#include "internal.h"
#include <math.h>

/* Advisory geometry checks, deliberately not an electrical netlist. Check
 * top-level objects, including expanded macro lines as neighbouring geometry. */
static int same_point(FcPoint a, FcPoint b) {
    return hypot(a.x - b.x, a.y - b.y) < 1e-7;
}

static int on_line(FcPoint q, const FcPrim *p) {
    FcPoint a = p->u.line.a, b = p->u.line.b;
    double dx = b.x - a.x, dy = b.y - a.y, len = hypot(dx, dy);
    double t;
    if (len < 1e-7) return 0;
    if (fabs(dx * (q.y - a.y) - dy * (q.x - a.x)) > 1e-7 * len) return 0;
    t = ((q.x - a.x) * dx + (q.y - a.y) * dy) / (len * len);
    return t >= -1e-10 && t <= 1 + 1e-10;
}

static int wire(const FcPrim *p, int layer) {
    return p->kind == FC_LINE && p->layer == layer && p->ext.dash == 0;
}

static int rays_at(const FcDrawing *d, FcPoint q, int layer, FcPoint rays[3]) {
    int i, j, k, n = 0;
    for (i = 0; i < d->nprims && n < 3; i++) {
        const FcPrim *p = &d->prims[i];
        FcPoint ends[2];
        if (!wire(p, layer) || !on_line(q, p)) continue;
        ends[0] = p->u.line.a; ends[1] = p->u.line.b;
        for (j = 0; j < 2 && n < 3; j++) {
            FcPoint v = {ends[j].x - q.x, ends[j].y - q.y};
            double len = hypot(v.x, v.y);
            if (len < 1e-7) continue;
            v.x /= len; v.y /= len;
            for (k = 0; k < n && !same_point(v, rays[k]); k++) {}
            if (k == n) rays[n++] = v;
        }
    }
    return n;
}

static int dot_at(const FcDrawing *d, FcPoint q, int layer) {
    int i;
    for (i = 0; i < d->nprims; i++)
        if (d->prims[i].kind == FC_CONN && d->prims[i].layer == layer &&
            same_point(q, d->prims[i].u.conn.p)) return 1;
    return 0;
}

static void finding(FcBuf *b, int *n, int json, const char *code, int index, int other,
                    FcPoint q, const char *message) {
    char x[32], y[32];
    fc_fmt_num(x, sizeof x, q.x); fc_fmt_num(y, sizeof y, q.y);
    if (json) {
        if (*n) fc_buf_putc(b, ',');
        fc_buf_printf(b, "{\"code\":\"%s\",\"index\":%d,\"other\":%d,\"x\":%s,\"y\":%s,\"message\":",
                      code, index, other, x, y);
        fc_json_escape(b, message); fc_buf_putc(b, '}');
    } else fc_buf_printf(b, "[%d] %s at (%s,%s): %s\n", index, code, x, y, message);
    (*n)++;
}

char *fc_lint(const FcDrawing *d, const FcLib *lib, int json) {
    FcBuf b;
    FcDrawing expanded;
    const FcDrawing *geometry = d;
    int i, j, n = 0, limited;
    fc_drawing_init(&expanded);
    if (lib) { fc_expand(d, &expanded, lib); geometry = &expanded; }
    limited = d->nprims > 4096 || geometry->nprims > 4096;
    fc_buf_init(&b);
    if (json) fc_buf_puts(&b, "{\"scope\":\"top-level solid LI and SA, same layer, including neighbouring macro lines; advisory, no electrical pin inference\",\"findings\":[");
    for (i = 0; !limited && i < d->nprims && n < 128; i++) {
        const FcPrim *p = &d->prims[i];
        FcPoint rays[3];
        if (p->kind == FC_CONN) {
            int degree = rays_at(geometry, p->u.conn.p, p->layer, rays);
            if (degree < 3) {
                int corner = degree == 2 && fabs(rays[0].x * rays[1].y - rays[0].y * rays[1].x) > 1e-7;
                finding(&b, &n, json, corner ? "corner-dot" : "dot-without-junction", i, -1,
                        p->u.conn.p, "Fewer than three wire directions; check whether this dot is needed.");
            }
        }
        if (!wire(p, p->layer)) continue;
        for (j = 0; j < 2 && n < 128; j++) {
            FcPoint q = j ? p->u.line.b : p->u.line.a;
            int k, seen = j && same_point(q, p->u.line.a);
            for (k = 0; k < i && !seen; k++)
                if (wire(&d->prims[k], p->layer) &&
                    (same_point(q, d->prims[k].u.line.a) || same_point(q, d->prims[k].u.line.b))) seen = 1;
            if (!seen && !dot_at(geometry, q, p->layer) && rays_at(geometry, q, p->layer, rays) >= 3)
                finding(&b, &n, json, "missing-junction-dot", i, -1, q,
                        "Wire endpoint meets a branch; add a dot if these wires are connected.");
        }
        for (j = i + 1; j < d->nprims && n < 128; j++) {
            const FcPrim *other = &d->prims[j];
            FcPoint a = p->u.line.a, z = p->u.line.b;
            double dx = z.x - a.x, dy = z.y - a.y, len = hypot(dx, dy), t0, t1;
            if (!wire(other, p->layer) || len < 1e-7) continue;
            if (fabs(dx * (other->u.line.a.y - a.y) - dy * (other->u.line.a.x - a.x)) > len * 1e-7 ||
                fabs(dx * (other->u.line.b.y - a.y) - dy * (other->u.line.b.x - a.x)) > len * 1e-7) continue;
            t0 = ((other->u.line.a.x - a.x) * dx + (other->u.line.a.y - a.y) * dy) / len;
            t1 = ((other->u.line.b.x - a.x) * dx + (other->u.line.b.y - a.y) * dy) / len;
            if (fmin(len, fmax(t0, t1)) - fmax(0, fmin(t0, t1)) > 1e-7)
                finding(&b, &n, json, "overlapping-wires", i, j, a, "Collinear wire segments overlap.");
        }
    }
    limited = limited || n >= 128;
    if (json) fc_buf_printf(&b, "],\"count\":%d,\"limited\":%s}\n", n, limited ? "true" : "false");
    else fc_buf_printf(&b, "%d advisory findings%s. Scope: top-level solid LI and SA, neighbouring macro lines, same layer; no electrical pin inference.\n",
                       n, limited ? " (limit reached: 4096 primitives / 128 findings)" : "");
    fc_drawing_free(&expanded);
    return fc_buf_steal(&b);
}
