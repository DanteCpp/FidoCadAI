#include "internal.h"

#include <math.h>

/* In-place editing, hit testing and diffing. Used by fcai-gui and `fcai diff`. */

int fc_append_fcd(FcDrawing *d, const char *text, size_t len) {
    int n0 = d->nprims;
    if (fc_parse(d, text, len) != FC_OK) return -1;
    return d->nprims - n0;
}

void fc_drawing_remove(FcDrawing *d, int index) {
    if (index < 0 || index >= d->nprims) return;
    memmove(&d->prims[index], &d->prims[index + 1],
            (size_t)(d->nprims - index - 1) * sizeof(FcPrim));
    d->nprims--;
}

int fc_drawing_move(FcDrawing *d, int from, int to) {
    FcPrim tmp;
    if (from < 0 || from >= d->nprims || to < 0 || to >= d->nprims) return FC_ERR_ARG;
    if (from == to) return FC_OK;
    tmp = d->prims[from];
    if (from < to)
        memmove(&d->prims[from], &d->prims[from + 1], (size_t)(to - from) * sizeof(FcPrim));
    else
        memmove(&d->prims[to + 1], &d->prims[to], (size_t)(from - to) * sizeof(FcPrim));
    d->prims[to] = tmp;
    return FC_OK;
}

static void shift(FcPoint *p, double dx, double dy) {
    p->x = fc_clamp_coord(p->x + dx);
    p->y = fc_clamp_coord(p->y + dy);
}

static int geometry_handles(const FcPrim *p) {
    int n = 0;
    if (p) fc_prim_points(p, &n);
    return n;
}

static int label_has_handle(int present, const FcLabel *label) {
    return present && label->text && label->text[0];
}

int fc_prim_handles(const FcPrim *p) {
    if (!p) return 0;
    return geometry_handles(p) + label_has_handle(p->has_name, &p->name) +
           label_has_handle(p->has_value, &p->value);
}

/* Geometry keeps its indices; append only the visible name/value handles.
 * Empty TY slots in an FCJ pair must not create invisible drag targets. */
static int label_handle_slot(const FcPrim *p, int i) {
    i -= geometry_handles(p);
    if (i < 0) return 0;
    if (label_has_handle(p->has_name, &p->name)) {
        if (i == 0) return 1;
        i--;
    }
    return i == 0 && label_has_handle(p->has_value, &p->value) ? 2 : 0;
}

int fc_prim_handle(const FcPrim *p, int i, FcPoint *out) {
    int label, n;
    if (!p || !out || i < 0 || i >= fc_prim_handles(p)) return 0;
    label = label_handle_slot(p, i);
    if (label) *out = label == 1 ? p->name.p : p->value.p;
    else *out = fc_prim_points(p, &n)[i];
    return 1;
}

void fc_prim_set_handle(FcPrim *p, int i, double x, double y) {
    FcPoint q;
    int label, n;
    if (!p || i < 0 || i >= fc_prim_handles(p)) return;
    q.x = fc_clamp_coord(x);
    q.y = fc_clamp_coord(y);
    label = label_handle_slot(p, i);
    if (label == 1) p->name.p = q;
    else if (label == 2) p->value.p = q;
    else fc_prim_points(p, &n)[i] = q;
}

void fc_prim_translate(FcPrim *p, double dx, double dy) {
    int i, n;
    FcPoint *pts = fc_prim_points(p, &n);
    for (i = 0; i < n; i++) shift(&pts[i], dx, dy);
    if (p->has_name) shift(&p->name.p, dx, dy);
    if (p->has_value) shift(&p->value.p, dx, dy);
}

/* fc_xform_prim works around the macro origin (100,100); move the pivot there first. */
void fc_prim_rotate(FcPrim *p, double cx, double cy) {
    fc_prim_translate(p, 100.0 - cx, 100.0 - cy);
    fc_xform_prim(p, 1, 0, cx, cy);
}

void fc_prim_mirror(FcPrim *p, double cx) {
    fc_prim_translate(p, 100.0 - cx, 0);
    fc_xform_prim(p, 0, 1, cx, 100.0);
}

/* ---- hit testing ---- */

static double seg_dist(FcPoint p, FcPoint a, FcPoint b) {
    double vx = b.x - a.x, vy = b.y - a.y;
    double wx = p.x - a.x, wy = p.y - a.y;
    double l2 = vx * vx + vy * vy, t = 0;
    if (l2 > 0) {
        t = (wx * vx + wy * vy) / l2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
    }
    wx = a.x + t * vx - p.x;
    wy = a.y + t * vy - p.y;
    return sqrt(wx * wx + wy * wy);
}

static double poly_dist(FcPoint p, const FcPoint *pts, int n, int closed) {
    double best = 1e30;
    int i;
    if (n == 1) return hypot(p.x - pts[0].x, p.y - pts[0].y);
    for (i = 0; i + 1 < n; i++) {
        double dd = seg_dist(p, pts[i], pts[i + 1]);
        if (dd < best) best = dd;
    }
    if (closed && n > 2) {
        double dd = seg_dist(p, pts[n - 1], pts[0]);
        if (dd < best) best = dd;
    }
    return best;
}

static int inside_poly(FcPoint p, const FcPoint *pts, int n) {
    int i, j, in = 0;
    for (i = 0, j = n - 1; i < n; j = i++) {
        if ((pts[i].y > p.y) != (pts[j].y > p.y) &&
            p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x)
            in = !in;
    }
    return in;
}

static double label_dist(FcPoint p, const FcLabel *l) {
    FcRect r;
    double w = (l->text ? (double)fc_strlen(l->text) : 1) * (l->size_x > 0 ? l->size_x : 3);
    double h = l->size_y > 0 ? l->size_y : 4;
    double dx, dy;
    r.min_x = l->p.x;
    r.min_y = l->p.y;
    r.max_x = l->p.x + w;
    r.max_y = l->p.y + h;
    dx = p.x < r.min_x ? r.min_x - p.x : p.x > r.max_x ? p.x - r.max_x : 0;
    dy = p.y < r.min_y ? r.min_y - p.y : p.y > r.max_y ? p.y - r.max_y : 0;
    return sqrt(dx * dx + dy * dy);
}

/* Distance from p to a non-macro primitive's ink (0 inside filled shapes). */
static double prim_dist(FcPoint p, const FcPrim *q) {
    double best = 1e30, dd;
    FcPoint pts[64];
    int i;
    switch (q->kind) {
    case FC_LINE:
        best = seg_dist(p, q->u.line.a, q->u.line.b);
        break;
    case FC_BEZIER:
        for (i = 0; i <= 32; i++) {
            double t = i / 32.0, u = 1 - t;
            pts[i].x = u * u * u * q->u.bezier.p[0].x + 3 * u * u * t * q->u.bezier.p[1].x +
                       3 * u * t * t * q->u.bezier.p[2].x + t * t * t * q->u.bezier.p[3].x;
            pts[i].y = u * u * u * q->u.bezier.p[0].y + 3 * u * u * t * q->u.bezier.p[1].y +
                       3 * u * t * t * q->u.bezier.p[2].y + t * t * t * q->u.bezier.p[3].y;
        }
        best = poly_dist(p, pts, 33, 0);
        break;
    case FC_RECT:
        pts[0] = q->u.box.a;
        pts[1].x = q->u.box.b.x;
        pts[1].y = q->u.box.a.y;
        pts[2] = q->u.box.b;
        pts[3].x = q->u.box.a.x;
        pts[3].y = q->u.box.b.y;
        best = q->filled && inside_poly(p, pts, 4) ? 0 : poly_dist(p, pts, 4, 1);
        break;
    case FC_OVAL: {
        double cx = (q->u.box.a.x + q->u.box.b.x) / 2, cy = (q->u.box.a.y + q->u.box.b.y) / 2;
        double rx = fabs(q->u.box.b.x - q->u.box.a.x) / 2, ry = fabs(q->u.box.b.y - q->u.box.a.y) / 2;
        for (i = 0; i < 48; i++) {
            double a = i * 6.283185307179586 / 48;
            pts[i].x = cx + rx * cos(a);
            pts[i].y = cy + ry * sin(a);
        }
        best = q->filled && inside_poly(p, pts, 48) ? 0 : poly_dist(p, pts, 48, 1);
        break;
    }
    case FC_POLY:
    case FC_CURVE:
        if (q->u.poly.n <= 0) break;
        best = q->filled && inside_poly(p, q->u.poly.pts, q->u.poly.n)
                   ? 0
                   : poly_dist(p, q->u.poly.pts, q->u.poly.n, q->kind == FC_POLY || q->u.poly.closed);
        break;
    case FC_CONN:
        best = hypot(p.x - q->u.conn.p.x, p.y - q->u.conn.p.y) - 1;
        break;
    case FC_PCB_LINE:
        best = seg_dist(p, q->u.pcb_line.a, q->u.pcb_line.b) - q->u.pcb_line.width / 2;
        break;
    case FC_PCB_PAD: {
        double r = (q->u.pad.rx > q->u.pad.ry ? q->u.pad.rx : q->u.pad.ry) / 2;
        best = hypot(p.x - q->u.pad.p.x, p.y - q->u.pad.p.y) - r;
        break;
    }
    case FC_MACRO:
        best = hypot(p.x - q->u.macro.p.x, p.y - q->u.macro.p.y);
        break;
    case FC_TEXT:
        best = label_dist(p, &q->u.text);
        break;
    }
    if (q->has_name && (dd = label_dist(p, &q->name)) < best) best = dd;
    if (q->has_value && (dd = label_dist(p, &q->value)) < best) best = dd;
    return best < 0 ? 0 : best;
}

int fc_hit_test(const FcDrawing *d, const FcLib *lib, double x, double y, double tol) {
    FcPoint p;
    FcDrawing exp;
    FcRect *box;
    double *dist, pick_area = 1e30;
    int *has, i, n = d->nprims, pick = -1;
    if (n <= 0 || !(dist = (double *)malloc((size_t)n * sizeof *dist))) return -1;
    p.x = x;
    p.y = y;
    /* Ink distance from one expansion: a macro is as near as its insertion
     * point, labels or nearest expanded primitive. */
    for (i = 0; i < n; i++) dist[i] = prim_dist(p, &d->prims[i]);
    fc_drawing_init(&exp);
    fc_expand(d, &exp, lib);
    for (i = 0; i < exp.nprims; i++) {
        int o = exp.prims[i].origin;
        double dd;
        if (o < 0 || o >= n || d->prims[o].kind != FC_MACRO) continue;
        dd = prim_dist(p, &exp.prims[i]);
        if (dd < dist[o]) dist[o] = dd;
    }
    fc_drawing_free(&exp);
    /* Topmost primitive whose ink is within tol wins. */
    for (i = n - 1; i >= 0 && dist[i] > tol; i--) {}
    free(dist);
    if (i >= 0) return i;
    /* Otherwise the smallest symbol whose bounding box contains the point. */
    box = (FcRect *)malloc((size_t)n * sizeof *box);
    has = (int *)malloc((size_t)n * sizeof *has);
    if (box && has) {
        fc_prims_bounds(d, lib, box, has);
        for (i = n - 1; i >= 0; i--) {
            const FcRect *r = &box[i];
            double area;
            if (d->prims[i].kind != FC_MACRO || !has[i]) continue;
            if (x < r->min_x - tol || x > r->max_x + tol || y < r->min_y - tol || y > r->max_y + tol)
                continue;
            area = (r->max_x - r->min_x + 1) * (r->max_y - r->min_y + 1);
            if (area < pick_area) {
                pick_area = area;
                pick = i;
            }
        }
    }
    free(box);
    free(has);
    return pick;
}

char *fc_prim_fcd(const FcPrim *p) {
    FcBuf b;
    fc_buf_init(&b);
    fc_emit_prim(&b, p, 1);
    return fc_buf_steal(&b); /* "" for a degenerate primitive */
}

static int anchor(const FcPrim *p, FcPoint *out) {
    int n;
    const FcPoint *pts = fc_prim_points(p, &n);
    if (!n) return 0;
    *out = pts[0];
    return 1;
}

int fc_prim_set_text(FcDrawing *d, int index, int which, const char *text) {
    FcPrim *p;
    FcLabel *l;
    int *has;
    FcPoint at;
    if (!d || index < 0 || index >= d->nprims || !text) return FC_ERR_ARG;
    p = &d->prims[index];
    if (which == 0) {
        if (p->kind != FC_TEXT) return FC_ERR_ARG;
        p->u.text.text = fc_arena_strdup(&d->arena, text);
        return p->u.text.text ? FC_OK : FC_ERR_NOMEM;
    }
    if (p->kind == FC_TEXT) return FC_ERR_ARG;
    l = which == 1 ? &p->name : &p->value;
    has = which == 1 ? &p->has_name : &p->has_value;
    if (!text[0]) {
        *has = 0;
        return FC_OK;
    }
    if (!*has) {
        if (!anchor(p, &at)) at.x = at.y = 0;
        fc_label_default(l, at.x + 10, at.y + (which == 1 ? 0 : 5), p->layer);
        *has = 1;
    }
    l->text = fc_arena_strdup(&d->arena, text);
    return l->text ? FC_OK : FC_ERR_NOMEM;
}

/* ---- diff ---- */

typedef struct {
    char **text;
    int *index; /* original primitive index */
    int n;
} PrimList;

static void list_build(PrimList *l, const FcDrawing *d) {
    int i;
    l->text = (char **)calloc((size_t)d->nprims + 1, sizeof(char *));
    l->index = (int *)calloc((size_t)d->nprims + 1, sizeof(int));
    l->n = 0;
    if (!l->text || !l->index) return;
    for (i = 0; i < d->nprims; i++) {
        char *s = fc_prim_fcd(&d->prims[i]);
        if (!s || !s[0]) {
            free(s);
            continue; /* degenerate primitives are never emitted */
        }
        l->text[l->n] = s;
        l->index[l->n] = i;
        l->n++;
    }
}

static void list_free(PrimList *l) {
    int i;
    for (i = 0; i < l->n; i++) free(l->text[i]);
    free(l->text);
    free(l->index);
}

/* Is `after` exactly `before` translated? Reports the offset. */
static int pure_move(const FcPrim *before, const FcPrim *after, const char *after_text, double *dx,
                     double *dy) {
    FcPoint a, b;
    FcDrawing tmp;
    char *s;
    int same = 0;
    if (before->kind != after->kind || !anchor(before, &a) || !anchor(after, &b)) return 0;
    *dx = b.x - a.x;
    *dy = b.y - a.y;
    if (*dx == 0 && *dy == 0) return 0;
    s = fc_prim_fcd(before);
    fc_drawing_init(&tmp);
    fc_append_fcd(&tmp, s, 0);
    free(s);
    if (tmp.nprims == 1) {
        fc_prim_translate(&tmp.prims[0], *dx, *dy);
        s = fc_prim_fcd(&tmp.prims[0]);
        same = s && fc_streq(s, after_text);
        free(s);
    }
    fc_drawing_free(&tmp);
    return same;
}

typedef struct {
    char op; /* '+', '-', '~' */
    int a, b; /* positions in PrimList a / b */
} DiffOp;

static int same_kind(const char *a, const char *b) {
    while (*a && *a != ' ' && *a == *b) a++, b++;
    return (*a == ' ' || !*a) && (*b == ' ' || !*b);
}

/* Rewrite a hunk of '-'/'+' ops so that a deletion and an addition of the same
 * primitive kind become one '~' modification. */
static void pair_hunk(DiffOp *hunk, int cnt, const PrimList *A, const PrimList *B, int *nops,
                      int d0) {
    DiffOp *out = (DiffOp *)malloc(sizeof(DiffOp) * (size_t)(cnt + 1));
    char *used = (char *)calloc((size_t)cnt + 1, 1);
    int k, q, w = 0;
    if (!out || !used) {
        free(out);
        free(used);
        return;
    }
    for (k = 0; k < cnt; k++) {
        if (hunk[k].op != '-') continue;
        out[w] = hunk[k];
        for (q = 0; q < cnt; q++) {
            if (hunk[q].op != '+' || used[q]) continue;
            if (!same_kind(A->text[hunk[k].a], B->text[hunk[q].b])) continue;
            used[q] = 1;
            out[w].op = '~';
            out[w].b = hunk[q].b;
            break;
        }
        w++;
    }
    for (q = 0; q < cnt; q++)
        if (hunk[q].op == '+' && !used[q]) out[w++] = hunk[q];
    memcpy(hunk, out, sizeof(DiffOp) * (size_t)w);
    *nops = d0 + w;
    free(out);
    free(used);
}

static void put_lines(FcBuf *b, const char *prefix, const char *text) {
    while (*text) {
        const char *nl = strchr(text, '\n');
        size_t n = nl ? (size_t)(nl - text) : fc_strlen(text);
        fc_buf_puts(b, prefix);
        fc_buf_putn(b, text, n);
        fc_buf_putc(b, '\n');
        text += n + (nl ? 1 : 0);
    }
}

static void json_str_trim(FcBuf *b, const char *s) {
    size_t n = fc_strlen(s);
    char *t = (char *)malloc(n + 1);
    if (!t) return;
    memcpy(t, s, n + 1);
    while (n && t[n - 1] == '\n') t[--n] = 0;
    fc_json_escape(b, t);
    free(t);
}

/* Layer colour, opacity and name (FJC L / FJC N) as they would be emitted. */
static int layer_differs(const FcLayer *a, const FcLayer *b) {
    char x[32], y[32];
    fc_fmt_num(x, sizeof x, a->alpha);
    fc_fmt_num(y, sizeof y, b->alpha);
    return a->rgb != b->rgb || !fc_streq(x, y) || !fc_streq(a->name, b->name);
}

static void put_layer(FcBuf *b, const FcLayer *l, int json) {
    char al[32];
    fc_fmt_num(al, sizeof al, l->alpha);
    if (json) {
        fc_buf_printf(b, "{\"rgb\":\"%06X\",\"alpha\":%s,\"name\":", (unsigned)l->rgb, al);
        fc_json_escape(b, l->name);
        fc_buf_putc(b, '}');
    } else {
        fc_buf_printf(b, "#%06X alpha %s \"%s\"", (unsigned)l->rgb, al, l->name);
    }
}

static void put_layer_changes(FcBuf *b, const FcDrawing *before, const FcDrawing *after, int json) {
    int i, n = 0;
    if (json) fc_buf_puts(b, ",\"layers\":[");
    for (i = 0; i < FC_MAX_LAYERS; i++) {
        if (!layer_differs(&before->layers[i], &after->layers[i])) continue;
        if (json) {
            fc_buf_printf(b, "%s{\"layer\":%d,\"before\":", n ? "," : "", i);
            put_layer(b, &before->layers[i], 1);
            fc_buf_puts(b, ",\"after\":");
            put_layer(b, &after->layers[i], 1);
            fc_buf_putc(b, '}');
        } else {
            fc_buf_printf(b, "L [%d] ", i);
            put_layer(b, &before->layers[i], 0);
            fc_buf_puts(b, " -> ");
            put_layer(b, &after->layers[i], 0);
            fc_buf_putc(b, '\n');
        }
        n++;
    }
    if (json) fc_buf_putc(b, ']');
}

char *fc_diff(const FcDrawing *before, const FcDrawing *after, int json) {
    PrimList A, B;
    DiffOp *ops = NULL;
    int nops = 0, pre = 0, suf = 0, n, m, i, j, nadd = 0, ndel = 0, nmod = 0, nlay = 0;
    int *lcs = NULL;
    FcBuf b;

    list_build(&A, before);
    list_build(&B, after);
    ops = (DiffOp *)malloc(sizeof(DiffOp) * (size_t)(A.n + B.n + 1));
    fc_buf_init(&b);
    if (!ops) goto done;

    while (pre < A.n && pre < B.n && fc_streq(A.text[pre], B.text[pre])) pre++;
    while (suf < A.n - pre && suf < B.n - pre &&
           fc_streq(A.text[A.n - 1 - suf], B.text[B.n - 1 - suf]))
        suf++;
    n = A.n - pre - suf;
    m = B.n - pre - suf;

    if ((double)(n + 1) * (double)(m + 1) <= 4e7)
        lcs = (int *)calloc((size_t)(n + 1) * (size_t)(m + 1), sizeof(int));
#define L(i, j) lcs[(size_t)(i) * (size_t)(m + 1) + (size_t)(j)]
    if (lcs) {
        for (i = n - 1; i >= 0; i--)
            for (j = m - 1; j >= 0; j--)
                L(i, j) = fc_streq(A.text[pre + i], B.text[pre + j])
                              ? L(i + 1, j + 1) + 1
                              : (L(i + 1, j) > L(i, j + 1) ? L(i + 1, j) : L(i, j + 1));
    }
    i = j = 0;
    while (i < n || j < m) {
        /* collect one hunk of deletions and additions, then pair them up */
        int d0 = nops;
        while (i < n || j < m) {
            if (lcs && i < n && j < m && fc_streq(A.text[pre + i], B.text[pre + j])) break;
            if (i < n && (j >= m || !lcs || L(i + 1, j) >= L(i, j + 1))) {
                ops[nops].op = '-';
                ops[nops].a = pre + i++;
                ops[nops++].b = -1;
            } else {
                ops[nops].op = '+';
                ops[nops].a = -1;
                ops[nops++].b = pre + j++;
            }
        }
        /* pair each deletion with the next unused addition of the same kind */
        pair_hunk(ops + d0, nops - d0, &A, &B, &nops, d0);
        if (i < n && j < m) {
            i++;
            j++;
        }
    }
#undef L
    free(lcs);

    for (i = 0; i < nops; i++) {
        if (ops[i].op == '+') nadd++;
        else if (ops[i].op == '-') ndel++;
        else nmod++;
    }

    for (i = 0; i < FC_MAX_LAYERS; i++) nlay += layer_differs(&before->layers[i], &after->layers[i]);

    /* "changes" counts primitive ops plus layer (colour/opacity/name) changes */
    if (json) {
        fc_buf_printf(&b,
                      "{\"changes\":%d,\"added\":%d,\"removed\":%d,\"modified\":%d,"
                      "\"layers_changed\":%d,\"ops\":[",
                      nops + nlay, nadd, ndel, nmod, nlay);
    } else if (!nops && !nlay) {
        fc_buf_puts(&b, "no changes\n");
    } else {
        fc_buf_printf(&b, "%d change%s: %d added, %d removed, %d modified", nops + nlay,
                      nops + nlay == 1 ? "" : "s", nadd, ndel, nmod);
        if (nlay) fc_buf_printf(&b, ", %d layer%s", nlay, nlay == 1 ? "" : "s");
        fc_buf_putc(&b, '\n');
        put_layer_changes(&b, before, after, 0);
    }
    for (i = 0; i < nops; i++) {
        const DiffOp *o = &ops[i];
        if (json && i) fc_buf_putc(&b, ',');
        if (o->op == '+') {
            if (json) {
                fc_buf_printf(&b, "{\"op\":\"add\",\"index\":%d,\"fcd\":", B.index[o->b]);
                json_str_trim(&b, B.text[o->b]);
                fc_buf_putc(&b, '}');
            } else {
                fc_buf_printf(&b, "+ [%d]\n", B.index[o->b]);
                put_lines(&b, "  + ", B.text[o->b]);
            }
        } else if (o->op == '-') {
            if (json) {
                fc_buf_printf(&b, "{\"op\":\"del\",\"index\":%d,\"fcd\":", A.index[o->a]);
                json_str_trim(&b, A.text[o->a]);
                fc_buf_putc(&b, '}');
            } else {
                fc_buf_printf(&b, "- [%d]\n", A.index[o->a]);
                put_lines(&b, "  - ", A.text[o->a]);
            }
        } else {
            double dx = 0, dy = 0;
            char dxs[32], dys[32];
            int mv = pure_move(&before->prims[A.index[o->a]], &after->prims[B.index[o->b]],
                               B.text[o->b], &dx, &dy);
            fc_fmt_num(dxs, sizeof dxs, dx);
            fc_fmt_num(dys, sizeof dys, dy);
            if (json) {
                fc_buf_printf(&b, "{\"op\":\"mod\",\"from\":%d,\"to\":%d,", A.index[o->a],
                              B.index[o->b]);
                if (mv) fc_buf_printf(&b, "\"moved\":[%s,%s],", dxs, dys);
                fc_buf_puts(&b, "\"before\":");
                json_str_trim(&b, A.text[o->a]);
                fc_buf_puts(&b, ",\"after\":");
                json_str_trim(&b, B.text[o->b]);
                fc_buf_putc(&b, '}');
            } else {
                if (A.index[o->a] == B.index[o->b])
                    fc_buf_printf(&b, "~ [%d]", A.index[o->a]);
                else
                    fc_buf_printf(&b, "~ [%d->%d]", A.index[o->a], B.index[o->b]);
                if (mv) fc_buf_printf(&b, " moved by (%s,%s)", dxs, dys);
                fc_buf_putc(&b, '\n');
                put_lines(&b, "  - ", A.text[o->a]);
                put_lines(&b, "  + ", B.text[o->b]);
            }
        }
    }
    if (json) {
        fc_buf_putc(&b, ']');
        put_layer_changes(&b, before, after, 1);
        fc_buf_puts(&b, "}\n");
    }

done:
    free(ops);
    list_free(&A);
    list_free(&B);
    return fc_buf_steal(&b);
}
