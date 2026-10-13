#include "internal.h"
#include <math.h>

static int nearly(double a, double b) { return fabs(a - b) < 1e-5; }

static const char *diag_level_name(FcDiagLevel lv) {
    return lv == FC_DIAG_ERR ? "error" : (lv == FC_DIAG_WARN ? "warn" : "note");
}

static const char *font_out(const char *font, char *tmp, size_t cap) {
    if (!font || font[0] == 0 || (font[0] == '*' && font[1] == 0) ||
        fc_streq(font, "Courier New"))
        return "*";
    size_t o = 0;
    const char *p = font;
    while (*p && o + 2 < cap) {
        if (*p == ' ') {
            tmp[o++] = '+';
            tmp[o++] = '+';
        } else {
            tmp[o++] = *p;
        }
        p++;
    }
    tmp[o] = 0;
    return tmp;
}

static void emit_label(FcBuf *b, const FcLabel *l) {
    char x[32], y[32], sy[32], sx[32], font[256];
    fc_fmt_num(x, sizeof x, l->p.x);
    fc_fmt_num(y, sizeof y, l->p.y);
    fc_fmt_num(sy, sizeof sy, l->size_y);
    fc_fmt_num(sx, sizeof sx, l->size_x);
    fc_buf_printf(b, "TY %s %s %s %s %d %d %d %s %s\n", x, y, sy, sx, l->rotation, l->style,
                  fc_clamp_layer(l->layer), font_out(l->font, font, sizeof font),
                  l->text ? l->text : "");
}

static int need_fcj(const FcPrim *p) {
    int caps = fc_kind_caps(p->kind);
    if (!(caps & FC_KIND_DASH)) return 0;
    if (p->has_name || p->has_value) return 1;
    if (p->ext.dash > 0) return 1;
    if (p->ext.width > 0) return 1;
    if ((caps & FC_KIND_ARROW) && p->ext.arrows) return 1;
    return 0;
}

static void emit_fcj(FcBuf *b, const FcPrim *p) {
    int text = (p->has_name || p->has_value) ? 1 : 0;
    char al[32], ah[32], w[40] = "";
    if (p->ext.width > 0) { /* optional: only when it overrides FJC B/C */
        w[0] = ' ';
        fc_fmt_num(w + 1, sizeof w - 1, p->ext.width);
    }
    if (fc_kind_caps(p->kind) & FC_KIND_ARROW) {
        fc_fmt_num(al, sizeof al, p->ext.arrow_len);
        fc_fmt_num(ah, sizeof ah, p->ext.arrow_half_w);
        fc_buf_printf(b, "FCJ %d %d %s %s %d%s %d\n", p->ext.arrows, p->ext.arrow_style, al, ah,
                      fc_clamp_dash(p->ext.dash), w, text);
    } else {
        fc_buf_printf(b, "FCJ %d%s %d\n", fc_clamp_dash(p->ext.dash), w, text);
    }
}

/* Name and value always go out as a pair (like FidoCadJS): the parser takes the two
 * TY lines after a labelled primitive, so writing only one would make it swallow a
 * following standalone TY on the next read. */
static void emit_labels(FcBuf *b, const FcPrim *p) {
    /* FidoCadJS saveText always writes the two TY lines as a pair; an absent
     * one is emitted empty. Its parser only registers the primitive when the
     * second TY line arrives, so never write just one. */
    FcLabel empty;
    if (!p->has_name && !p->has_value) return;
    empty = p->has_name ? p->name : p->value;
    empty.text = "";
    emit_label(b, p->has_name ? &p->name : &empty);
    emit_label(b, p->has_value ? &p->value : &empty);
}

static int drop_degenerate(const FcPrim *p) {
    if (p->has_name || p->has_value) return 0;
    if (p->kind == FC_LINE && p->u.line.a.x == p->u.line.b.x && p->u.line.a.y == p->u.line.b.y)
        return 1;
    if (p->kind == FC_CURVE && p->u.poly.n < 2) return 1;
    return 0;
}

/* Code, coordinates, the kind's own fields, layer; then FCJ and the labels. */
void fc_emit_prim(FcBuf *b, const FcPrim *p, int extensions) {
    char x[32], y[32], w[3][32];
    int i, n;
    const FcPoint *pts = fc_prim_points(p, &n);
    if (drop_degenerate(p)) return;
    if (p->kind == FC_TEXT) {
        emit_label(b, &p->u.text); /* text has no FCJ/labels */
        return;
    }
    fc_buf_puts(b, fc_kind_code(p));
    if (p->kind == FC_CURVE) fc_buf_printf(b, " %d", p->u.poly.closed ? 1 : 0);
    for (i = 0; i < n; i++) {
        fc_fmt_num(x, sizeof x, pts[i].x);
        fc_fmt_num(y, sizeof y, pts[i].y);
        fc_buf_printf(b, " %s %s", x, y);
    }
    if (p->kind == FC_PCB_LINE) {
        fc_fmt_num(w[0], 32, p->u.pcb_line.width);
        fc_buf_printf(b, " %s", w[0]);
    } else if (p->kind == FC_PCB_PAD) {
        fc_fmt_num(w[0], 32, p->u.pad.rx);
        fc_fmt_num(w[1], 32, p->u.pad.ry);
        fc_fmt_num(w[2], 32, p->u.pad.hole);
        fc_buf_printf(b, " %s %s %s %d", w[0], w[1], w[2], p->u.pad.style);
    }
    if (p->kind == FC_MACRO) /* no layer: the macro's own primitives carry theirs */
        fc_buf_printf(b, " %d %d %s\n", p->u.macro.orient, p->u.macro.mirror ? 1 : 0,
                      p->u.macro.key ? p->u.macro.key : "");
    else
        fc_buf_printf(b, " %d\n", p->layer);
    if (extensions && (fc_kind_caps(p->kind) & FC_KIND_LABELS) && (p->has_name || p->has_value))
        fc_buf_puts(b, "FCJ\n"); /* FidoCadJS saveText: labels need this marker */
    if (extensions && need_fcj(p)) emit_fcj(b, p);
    emit_labels(b, p);
}

static void emit_config(FcBuf *b, const FcDrawing *d) {
    char n[32];
    int i;
    if (!nearly(d->cfg.diameter_conn, 2.0)) {
        fc_fmt_num(n, 32, d->cfg.diameter_conn);
        fc_buf_printf(b, "FJC C %s\n", n);
    }
    for (i = 0; i < FC_MAX_LAYERS; i++) {
        if (!d->layers[i].modified) continue;
        fc_fmt_num(n, 32, d->layers[i].alpha);
        fc_buf_printf(b, "FJC L %d %u %s\n", i, (unsigned)d->layers[i].rgb, n);
        if (!fc_streq(d->layers[i].name, fc_layer_default_name(i)))
            fc_buf_printf(b, "FJC N %d %s\n", i, d->layers[i].name);
    }
    if (!nearly(d->cfg.line_width, 0.5)) {
        fc_fmt_num(n, 32, d->cfg.line_width);
        fc_buf_printf(b, "FJC A %s\n", n);
    }
    if (!nearly(d->cfg.line_width_circles, 0.35)) {
        fc_fmt_num(n, 32, d->cfg.line_width_circles);
        fc_buf_printf(b, "FJC B %s\n", n);
    }
    if (d->cfg.has_img) {
        char x[32], y[32], s[32], a[32];
        fc_fmt_num(x, 32, d->cfg.img_x);
        fc_fmt_num(y, 32, d->cfg.img_y);
        fc_fmt_num(s, 32, d->cfg.img_scale);
        fc_fmt_num(a, 32, d->cfg.img_alpha);
        fc_buf_printf(b, "FJC IMG %s %s %s %s\n", x, y, s, a);
    }
}

size_t fc_emit(const FcDrawing *d, char **out, const FcEmitOpts *opts) {
    FcEmitOpts o;
    FcBuf b;
    int i;
    o.extensions = 1;
    o.header = 1;
    if (opts) o = *opts;
    fc_buf_init(&b);
    if (o.header) fc_buf_puts(&b, "[FIDOCAD]\n");
    if (o.extensions) emit_config(&b, d);
    for (i = 0; i < d->nprims; i++) fc_emit_prim(&b, &d->prims[i], o.extensions);
    *out = fc_buf_steal(&b);
    return *out ? fc_strlen(*out) : 0;
}

int fc_emit_file(const FcDrawing *d, const char *path, const FcEmitOpts *opts) {
    char *s = NULL;
    size_t n = fc_emit(d, &s, opts);
    int rc = s ? fc_write_file(path, s, n) : FC_ERR_NOMEM;
    free(s);
    return rc;
}

static void json_label(FcBuf *b, const char *key, const FcLabel *l) {
    char n[32];
    fc_buf_printf(b, "\"%s\":{", key);
    fc_fmt_num(n, 32, l->p.x);
    fc_buf_printf(b, "\"x\":%s,", n);
    fc_fmt_num(n, 32, l->p.y);
    fc_buf_printf(b, "\"y\":%s,", n);
    fc_fmt_num(n, 32, l->size_y);
    fc_buf_printf(b, "\"size_y\":%s,", n);
    fc_fmt_num(n, 32, l->size_x);
    fc_buf_printf(b, "\"size_x\":%s,", n);
    fc_buf_printf(b, "\"rotation\":%d,\"style\":%d,\"layer\":%d,\"font\":", l->rotation, l->style,
                  l->layer);
    fc_json_escape(b, l->font ? l->font : "*");
    fc_buf_puts(b, ",\"text\":");
    fc_json_escape(b, l->text ? l->text : "");
    fc_buf_puts(b, "}");
}

static void json_point(FcBuf *b, const FcPoint *p) {
    char x[32], y[32];
    fc_fmt_num(x, 32, p->x);
    fc_fmt_num(y, 32, p->y);
    fc_buf_printf(b, "{\"x\":%s,\"y\":%s}", x, y);
}

static void json_prim(FcBuf *b, const FcPrim *p) {
    int i, n;
    const FcPoint *pts = fc_prim_points(p, &n);
    fc_buf_puts(b, "{");
    fc_buf_printf(b, "\"kind\":");
    fc_json_escape(b, fc_kind_name(p->kind));
    fc_buf_printf(b, ",\"code\":");
    fc_json_escape(b, fc_kind_code(p));
    fc_buf_printf(b, ",\"layer\":%d,\"line\":%d", p->layer, p->src_line);
    if (p->filled) fc_buf_puts(b, ",\"filled\":true");
    if (p->unresolved) fc_buf_puts(b, ",\"unresolved\":true");
    /* geometry: a text label, a point list, two ends "a"/"b", or a position "p" */
    if (p->kind == FC_TEXT) {
        fc_buf_putc(b, ',');
        json_label(b, "text", &p->u.text);
    } else if (p->kind == FC_BEZIER || p->kind == FC_POLY || p->kind == FC_CURVE) {
        if (p->kind == FC_CURVE)
            fc_buf_printf(b, ",\"closed\":%s", p->u.poly.closed ? "true" : "false");
        fc_buf_puts(b, ",\"points\":[");
        for (i = 0; i < n; i++) {
            if (i) fc_buf_putc(b, ',');
            json_point(b, &pts[i]);
        }
        fc_buf_putc(b, ']');
    } else if (n == 2) {
        fc_buf_puts(b, ",\"a\":");
        json_point(b, &pts[0]);
        fc_buf_puts(b, ",\"b\":");
        json_point(b, &pts[1]);
    } else {
        fc_buf_puts(b, ",\"p\":");
        json_point(b, &pts[0]);
    }
    if (p->kind == FC_PCB_LINE) {
        char w[32];
        fc_fmt_num(w, 32, p->u.pcb_line.width);
        fc_buf_printf(b, ",\"width\":%s", w);
    } else if (p->kind == FC_PCB_PAD) {
        char rx[32], ry[32], h[32];
        fc_fmt_num(rx, 32, p->u.pad.rx);
        fc_fmt_num(ry, 32, p->u.pad.ry);
        fc_fmt_num(h, 32, p->u.pad.hole);
        fc_buf_printf(b, ",\"rx\":%s,\"ry\":%s,\"hole\":%s,\"style\":%d", rx, ry, h, p->u.pad.style);
    } else if (p->kind == FC_MACRO) {
        fc_buf_printf(b, ",\"orient\":%d,\"mirror\":%s,\"key\":", p->u.macro.orient,
                      p->u.macro.mirror ? "true" : "false");
        fc_json_escape(b, p->u.macro.key ? p->u.macro.key : "");
    }
    if (p->has_ext || p->ext.dash || p->ext.arrows || p->ext.width > 0) {
        char al[32], ah[32], w[32];
        fc_fmt_num(al, 32, p->ext.arrow_len);
        fc_fmt_num(ah, 32, p->ext.arrow_half_w);
        fc_fmt_num(w, 32, p->ext.width);
        fc_buf_printf(b, ",\"ext\":{\"arrows\":%d,\"arrow_style\":%d,\"arrow_len\":%s,\"arrow_half_w\":%s,\"dash\":%d,\"width\":%s}",
                      p->ext.arrows, p->ext.arrow_style, al, ah, p->ext.dash, w);
    }
    if (p->has_name) {
        fc_buf_putc(b, ',');
        json_label(b, "name", &p->name);
    }
    if (p->has_value) {
        fc_buf_putc(b, ',');
        json_label(b, "value", &p->value);
    }
    fc_buf_putc(b, '}');
}

static void include_pt(FcRect *r, FcPoint pt, int *any) {
    if (!*any) {
        r->min_x = r->max_x = pt.x;
        r->min_y = r->max_y = pt.y;
        *any = 1;
        return;
    }
    if (pt.x < r->min_x) r->min_x = pt.x;
    if (pt.y < r->min_y) r->min_y = pt.y;
    if (pt.x > r->max_x) r->max_x = pt.x;
    if (pt.y > r->max_y) r->max_y = pt.y;
}

static void include_label(FcRect *r, const FcLabel *l, int *any) {
    FcPoint q;
    double w;
    include_pt(r, l->p, any);
    w = (l->text ? (double)fc_strlen(l->text) : 1) * (l->size_x > 0 ? l->size_x : 3);
    q.x = l->p.x + w;
    q.y = l->p.y + (l->size_y > 0 ? l->size_y : 4);
    include_pt(r, q, any);
}

static void include_prim_bounds(FcRect *r, const FcPrim *p, int *any) {
    int i, n;
    const FcPoint *pts = fc_prim_points(p, &n);
    if (p->kind == FC_TEXT) {
        include_label(r, &p->u.text, any);
    } else if (p->kind == FC_MACRO) {
        /* only unexpanded macros reach here; leave room for the render placeholder */
        FcPoint q = p->u.macro.p;
        include_pt(r, q, any);
        q.x += FC_PLACEHOLDER_SIZE;
        q.y += FC_PLACEHOLDER_SIZE;
        include_pt(r, q, any);
        q.x = fc_clamp_coord(p->u.macro.p.x - FC_PLACEHOLDER_SIZE);
        q.y = fc_clamp_coord(p->u.macro.p.y - FC_PLACEHOLDER_SIZE);
        include_pt(r, q, any);
        if (p->u.macro.key) {
            FcLabel l;
            fc_label_default(&l, p->u.macro.p.x + FC_PLACEHOLDER_SIZE,
                             p->u.macro.p.y - FC_PLACEHOLDER_SIZE, 0);
            l.text = p->u.macro.key;
            include_label(r, &l, any);
        }
    } else {
        for (i = 0; i < n; i++) include_pt(r, pts[i], any);
    }
    if (p->has_name) include_label(r, &p->name, any);
    if (p->has_value) include_label(r, &p->value, any);
}

int fc_bounds(const FcDrawing *d, const FcLib *lib, FcRect *out) {
    FcDrawing exp;
    const FcDrawing *src = d;
    int any = 0;
    int i;
    FcRect r = {0, 0, 0, 0};
    if (lib) {
        fc_drawing_init(&exp);
        if (fc_expand(d, &exp, lib) == FC_OK) src = &exp;
    }
    for (i = 0; i < src->nprims; i++) include_prim_bounds(&r, &src->prims[i], &any);
    if (lib) fc_drawing_free(&exp);
    if (!any) {
        out->min_x = out->min_y = out->max_x = out->max_y = 0;
        return 0;
    }
    *out = r;
    return 1;
}

void fc_prims_bounds(const FcDrawing *d, const FcLib *lib, FcRect *out, int *has) {
    FcDrawing exp;
    int i, last;
    if (d->nprims <= 0) return;
    memset(out, 0, (size_t)d->nprims * sizeof *out);
    memset(has, 0, (size_t)d->nprims * sizeof *has);
    fc_drawing_init(&exp);
    last = fc_expand(d, &exp, lib) == FC_OK ? d->nprims - 1
                                            : (exp.nprims ? exp.prims[exp.nprims - 1].origin : -1);
    for (i = 0; i < exp.nprims; i++) {
        int o = exp.prims[i].origin;
        if (o >= 0 && o < d->nprims) include_prim_bounds(&out[o], &exp.prims[i], &has[o]);
    }
    /* past an expansion stopped over budget, primitives keep their own outline */
    for (i = last + 1; i < d->nprims; i++) include_prim_bounds(&out[i], &d->prims[i], &has[i]);
    fc_drawing_free(&exp);
}

void fc_stats(const FcDrawing *d, const FcLib *lib, FcStats *out) {
    int i, used = 0;
    memset(out, 0, sizeof *out);
    out->nprims = d->nprims;
    out->ndiags = d->ndiags;
    for (i = 0; i < d->ndiags; i++) {
        if (d->diags[i].level == FC_DIAG_WARN) out->nwarns++;
        if (d->diags[i].level == FC_DIAG_ERR) out->nerrs++;
    }
    for (i = 0; i < d->nprims; i++) {
        const FcPrim *p = &d->prims[i];
        if ((int)p->kind >= 0 && (int)p->kind < 11) out->by_kind[p->kind]++;
        if (p->kind == FC_MACRO) {
            out->nmacros++;
            if (p->unresolved || (lib && !fc_lib_find(lib, p->u.macro.key))) out->nunresolved++;
        }
        if (p->has_name || p->has_value) out->nlabels++;
        if (p->kind == FC_TEXT) out->nlabels++;
        if (p->layer >= 0 && p->layer < FC_MAX_LAYERS) used |= (1 << p->layer);
    }
    for (i = 0; i < FC_MAX_LAYERS; i++)
        if (used & (1 << i)) out->layers_used++;
    out->has_bbox = fc_bounds(d, lib, &out->bbox);
}

static void describe_prim(FcBuf *b, const FcPrim *p, int i) {
    char a[32], c[32], e[32], f[32];
    fc_buf_printf(b, "  [%d] L%d %s", i, p->layer, fc_kind_code(p));
    switch (p->kind) {
    case FC_LINE:
        fc_fmt_num(a, 32, p->u.line.a.x);
        fc_fmt_num(c, 32, p->u.line.a.y);
        fc_fmt_num(e, 32, p->u.line.b.x);
        fc_fmt_num(f, 32, p->u.line.b.y);
        fc_buf_printf(b, " (%s,%s)->(%s,%s)", a, c, e, f);
        break;
    case FC_MACRO:
        fc_fmt_num(a, 32, p->u.macro.p.x);
        fc_fmt_num(c, 32, p->u.macro.p.y);
        fc_buf_printf(b, " %s @(%s,%s) o=%d m=%d", p->u.macro.key ? p->u.macro.key : "?", a, c,
                      p->u.macro.orient, p->u.macro.mirror);
        if (p->unresolved) fc_buf_puts(b, " UNRESOLVED");
        break;
    case FC_TEXT:
        fc_fmt_num(a, 32, p->u.text.p.x);
        fc_fmt_num(c, 32, p->u.text.p.y);
        fc_buf_printf(b, " @(%s,%s) \"", a, c);
        fc_buf_puts(b, p->u.text.text ? p->u.text.text : "");
        fc_buf_putc(b, '"');
        break;
    case FC_CONN:
        fc_fmt_num(a, 32, p->u.conn.p.x);
        fc_fmt_num(c, 32, p->u.conn.p.y);
        fc_buf_printf(b, " @(%s,%s)", a, c);
        break;
    case FC_RECT:
    case FC_OVAL:
        fc_fmt_num(a, 32, p->u.box.a.x);
        fc_fmt_num(c, 32, p->u.box.a.y);
        fc_fmt_num(e, 32, p->u.box.b.x);
        fc_fmt_num(f, 32, p->u.box.b.y);
        fc_buf_printf(b, " (%s,%s)-(%s,%s)%s", a, c, e, f, p->filled ? " filled" : "");
        break;
    default:
        break;
    }
    if (p->ext.width > 0) {
        fc_fmt_num(a, 32, p->ext.width);
        fc_buf_printf(b, " width=%s", a);
    }
    if (p->has_name && p->name.text && p->name.text[0])
        fc_buf_printf(b, " name=\"%s\"", p->name.text);
    if (p->has_value && p->value.text && p->value.text[0])
        fc_buf_printf(b, " value=\"%s\"", p->value.text);
    fc_buf_putc(b, '\n');
}

char *fc_describe(const FcDrawing *d, const FcLib *lib, const FcDescribeOpts *opts) {
    FcBuf b;
    FcStats st;
    int i, verbose = opts && opts->verbose;
    fc_stats(d, lib, &st);
    fc_buf_init(&b);
    fc_buf_printf(&b, "FidoCad drawing: %d primitives", st.nprims);
    if (st.has_bbox) {
        char n[4][32];
        fc_fmt_num(n[0], 32, st.bbox.min_x);
        fc_fmt_num(n[1], 32, st.bbox.min_y);
        fc_fmt_num(n[2], 32, st.bbox.max_x);
        fc_fmt_num(n[3], 32, st.bbox.max_y);
        fc_buf_printf(&b, ", bbox [%s,%s]–[%s,%s]", n[0], n[1], n[2], n[3]);
    }
    fc_buf_printf(&b, ", %d macros", st.nmacros);
    if (st.nunresolved) fc_buf_printf(&b, " (%d unresolved)", st.nunresolved);
    fc_buf_putc(&b, '\n');
    fc_buf_printf(&b, "kinds: LI=%d BE=%d RV/RP=%d EV/EP=%d PV/PP=%d CV/CP=%d SA=%d PL=%d PA=%d MC=%d TY=%d\n",
                  st.by_kind[FC_LINE], st.by_kind[FC_BEZIER], st.by_kind[FC_RECT],
                  st.by_kind[FC_OVAL], st.by_kind[FC_POLY], st.by_kind[FC_CURVE],
                  st.by_kind[FC_CONN], st.by_kind[FC_PCB_LINE], st.by_kind[FC_PCB_PAD],
                  st.by_kind[FC_MACRO], st.by_kind[FC_TEXT]);
    fc_buf_puts(&b, "layers:");
    {
        int any = 0;
        for (i = 0; i < FC_MAX_LAYERS; i++) {
            int j, used = 0;
            for (j = 0; j < d->nprims; j++)
                if (d->prims[j].layer == i) used = 1;
            if (!used && !d->layers[i].modified) continue;
            fc_buf_printf(&b, "%s %d=%s", any ? "," : "", i, d->layers[i].name);
            any = 1;
        }
        if (!any) fc_buf_puts(&b, " (none)");
    }
    fc_buf_putc(&b, '\n');
    if (st.nmacros) {
        fc_buf_puts(&b, "macros:\n");
        for (i = 0; i < d->nprims; i++) {
            const FcPrim *p = &d->prims[i];
            if (p->kind != FC_MACRO) continue;
            fc_buf_printf(&b, "  %s", p->u.macro.key ? p->u.macro.key : "?");
            if (p->has_name && p->name.text && p->name.text[0])
                fc_buf_printf(&b, "  %s", p->name.text);
            if (p->has_value && p->value.text && p->value.text[0])
                fc_buf_printf(&b, "=%s", p->value.text);
            if (lib) {
                const FcMacro *m = fc_lib_find(lib, p->u.macro.key);
                if (m && m->long_name && m->long_name[0])
                    fc_buf_printf(&b, "  (%s)", m->long_name);
                else if (!m) fc_buf_puts(&b, "  UNRESOLVED");
            }
            fc_buf_putc(&b, '\n');
        }
    }
    if (d->ndiags) {
        fc_buf_printf(&b, "diagnostics: %d\n", d->ndiags);
        for (i = 0; i < d->ndiags; i++) {
            fc_buf_printf(&b, "  %s:%d: %s\n", diag_level_name(d->diags[i].level),
                          d->diags[i].line, d->diags[i].msg ? d->diags[i].msg : "");
        }
    } else {
        fc_buf_puts(&b, "diagnostics: none\n");
    }
    if (verbose) {
        fc_buf_puts(&b, "primitives:\n");
        for (i = 0; i < d->nprims; i++) describe_prim(&b, &d->prims[i], i);
    }
    return fc_buf_steal(&b);
}

char *fc_to_json(const FcDrawing *d, const FcLib *lib) {
    FcBuf b;
    FcStats st;
    int i;
    char n[32];
    fc_stats(d, lib, &st);
    fc_buf_init(&b);
    fc_buf_puts(&b, "{\n");
    fc_buf_printf(&b, "  \"version\": \"%s\",\n", FC_VERSION);
    fc_fmt_num(n, 32, d->cfg.diameter_conn);
    fc_buf_printf(&b, "  \"config\": {\"diameter_conn\": %s,", n);
    fc_fmt_num(n, 32, d->cfg.line_width);
    fc_buf_printf(&b, " \"line_width\": %s,", n);
    fc_fmt_num(n, 32, d->cfg.line_width_circles);
    fc_buf_printf(&b, " \"line_width_circles\": %s},\n", n);
    fc_buf_puts(&b, "  \"stats\": {");
    fc_buf_printf(&b, "\"primitives\": %d, \"macros\": %d, \"unresolved_macros\": %d, \"labels\": %d, \"layers_used\": %d, \"warnings\": %d, \"errors\": %d",
                  st.nprims, st.nmacros, st.nunresolved, st.nlabels, st.layers_used, st.nwarns,
                  st.nerrs);
    if (st.has_bbox) {
        char x0[32], y0[32], x1[32], y1[32];
        fc_fmt_num(x0, 32, st.bbox.min_x);
        fc_fmt_num(y0, 32, st.bbox.min_y);
        fc_fmt_num(x1, 32, st.bbox.max_x);
        fc_fmt_num(y1, 32, st.bbox.max_y);
        fc_buf_printf(&b, ", \"bbox\": {\"min_x\": %s, \"min_y\": %s, \"max_x\": %s, \"max_y\": %s}",
                      x0, y0, x1, y1);
    }
    fc_buf_puts(&b, "},\n");
    fc_buf_puts(&b, "  \"layers\": [");
    for (i = 0; i < FC_MAX_LAYERS; i++) {
        if (i) fc_buf_putc(&b, ',');
        fc_buf_printf(&b, "{\"id\":%d,\"name\":", i);
        fc_json_escape(&b, d->layers[i].name);
        fc_fmt_num(n, 32, d->layers[i].alpha);
        fc_buf_printf(&b, ",\"rgb\":%u,\"alpha\":%s,\"modified\":%s}", (unsigned)d->layers[i].rgb,
                      n, d->layers[i].modified ? "true" : "false");
    }
    fc_buf_puts(&b, "],\n");
    fc_buf_puts(&b, "  \"diagnostics\": [");
    for (i = 0; i < d->ndiags; i++) {
        if (i) fc_buf_putc(&b, ',');
        fc_buf_printf(&b, "{\"level\":");
        fc_json_escape(&b, diag_level_name(d->diags[i].level));
        fc_buf_printf(&b, ",\"line\":%d,\"msg\":", d->diags[i].line);
        fc_json_escape(&b, d->diags[i].msg);
        fc_buf_putc(&b, '}');
    }
    fc_buf_puts(&b, "],\n");
    fc_buf_puts(&b, "  \"primitives\": [");
    for (i = 0; i < d->nprims; i++) {
        if (i) fc_buf_putc(&b, ',');
        json_prim(&b, &d->prims[i]);
    }
    fc_buf_puts(&b, "]\n}\n");
    return fc_buf_steal(&b);
}

char *fc_context(const FcDrawing *d, const FcLib *lib) {
    FcDescribeOpts o;
    FcBuf b;
    char *desc, *js;
    o.verbose = 1;
    desc = fc_describe(d, lib, &o);
    js = fc_to_json(d, lib);
    fc_buf_init(&b);
    fc_buf_puts(&b, "=== FidoCadAI context ===\n");
    if (desc) fc_buf_puts(&b, desc);
    fc_buf_puts(&b, "\n=== json ===\n");
    if (js) fc_buf_puts(&b, js);
    free(desc);
    free(js);
    return fc_buf_steal(&b);
}

int fc_validate(const FcDrawing *d, char **report) {
    FcEmitOpts opts;
    FcDrawing d2, d3;
    char *e1 = NULL, *e2 = NULL;
    FcBuf b;
    int ok = 1;
    int i, ndropped = 0;
    opts.extensions = 1;
    opts.header = 1;
    fc_buf_init(&b);
    fc_emit(d, &e1, &opts);
    fc_drawing_init(&d2);
    fc_parse(&d2, e1, fc_strlen(e1));
    fc_emit(&d2, &e2, &opts);
    if (!e1 || !e2 || !fc_streq(e1, e2)) {
        ok = 0;
        fc_buf_puts(&b, "ROUNDTRIP FAIL: emit is not idempotent\n");
        if (e1) fc_buf_printf(&b, "--- first emit (%zu bytes) ---\n%s", fc_strlen(e1), e1);
        if (e2) fc_buf_printf(&b, "--- second emit (%zu bytes) ---\n%s", fc_strlen(e2), e2);
    } else {
        fc_buf_printf(&b, "round-trip ok (%zu bytes, %d primitives)\n", fc_strlen(e1), d2.nprims);
    }
    /* zero-length lines and one-point curves are dropped on purpose by the emitter */
    for (i = 0; i < d->nprims; i++)
        if (drop_degenerate(&d->prims[i])) ndropped++;
    if (ndropped)
        fc_buf_printf(&b, "note: %d degenerate primitive%s dropped on emit\n", ndropped,
                      ndropped == 1 ? "" : "s");
    if (d->nprims - ndropped != d2.nprims) {
        ok = 0;
        fc_buf_printf(&b, "primitive count %d -> %d\n", d->nprims - ndropped, d2.nprims);
    }
    fc_drawing_init(&d3);
    fc_parse(&d3, e2, fc_strlen(e2));
    if (d2.nprims != d3.nprims) {
        ok = 0;
        fc_buf_printf(&b, "second parse primitive count %d -> %d\n", d2.nprims, d3.nprims);
    }
    if (d->ndiags) {
        fc_buf_printf(&b, "parse diagnostics: %d\n", d->ndiags);
        for (i = 0; i < d->ndiags; i++)
            fc_buf_printf(&b, "  %s:%d: %s\n", diag_level_name(d->diags[i].level),
                          d->diags[i].line, d->diags[i].msg ? d->diags[i].msg : "");
    }
    fc_drawing_free(&d2);
    fc_drawing_free(&d3);
    free(e1);
    free(e2);
    *report = fc_buf_steal(&b);
    return ok ? FC_OK : FC_ERR_ARG;
}
