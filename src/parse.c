#include "internal.h"

typedef struct {
    FcDrawing *d;
    FcPrim pend;
    int has_pend;
    int expect_fcj;
    int labels_left;
    int line;
} ParseCtx;

static double tok_coord(const char *s) {
    double v;
    if (!fc_parse_double(s, &v)) return 0;
    return fc_clamp_coord(v);
}

static int tok_int(const char *s, int def) {
    int v;
    if (!fc_parse_int(s, &v)) return def;
    return v;
}

static double tok_dbl(const char *s, double def) {
    double v;
    if (!fc_parse_double(s, &v)) return def;
    return v;
}

static int tok_layer(const char *s) {
    int v;
    if (!fc_parse_int(s, &v)) return 0;
    return fc_clamp_layer(v);
}

static char *join_rest(FcArena *a, const char **tok, int ntok, int start) {
    if (start >= ntok) return fc_arena_strdup(a, "");
    size_t total = 0;
    int i;
    for (i = start; i < ntok; i++) total += fc_strlen(tok[i]) + 1;
    char *out = (char *)fc_arena_alloc(a, total + 1);
    if (!out) return NULL;
    char *p = out;
    for (i = start; i < ntok; i++) {
        size_t n = fc_strlen(tok[i]);
        memcpy(p, tok[i], n);
        p += n;
        if (i + 1 < ntok) *p++ = ' ';
    }
    *p = 0;
    return out;
}

static char *decode_font(FcArena *a, const char *raw) {
    if (!raw || (raw[0] == '*' && raw[1] == 0)) return fc_arena_strdup(a, "*");
    size_t n = fc_strlen(raw);
    char *out = (char *)fc_arena_alloc(a, n + 1);
    if (!out) return NULL;
    size_t i = 0, o = 0;
    while (i < n) {
        if (raw[i] == '+' && i + 1 < n && raw[i + 1] == '+') {
            out[o++] = ' ';
            i += 2;
        } else {
            out[o++] = raw[i++];
        }
    }
    out[o] = 0;
    return out;
}

static int parse_label(FcDrawing *d, FcLabel *l, const char **tok, int ntok) {
    if (ntok < 9) return 0;
    l->p.x = tok_coord(tok[1]);
    l->p.y = tok_coord(tok[2]);
    l->size_y = tok_dbl(tok[3], 4);
    l->size_x = tok_dbl(tok[4], 3);
    l->rotation = tok_int(tok[5], 0);
    l->style = tok_int(tok[6], 0);
    l->layer = tok_layer(tok[7]);
    l->font = decode_font(&d->arena, tok[8]);
    l->text = join_rest(&d->arena, tok, ntok, 9);
    return 1;
}

static int parse_te(FcDrawing *d, FcLabel *l, const char **tok, int ntok) {
    if (ntok < 4) return 0;
    l->p.x = tok_coord(tok[1]);
    l->p.y = tok_coord(tok[2]);
    l->size_y = 4;
    l->size_x = 3;
    l->rotation = 0;
    l->style = 0;
    l->layer = 0;
    l->font = "*";
    l->text = join_rest(&d->arena, tok, ntok, 3);
    return 1;
}

static FcPoint *copy_pts(FcDrawing *d, FcPoint *pts, int n) {
    FcPoint *p = (FcPoint *)fc_arena_alloc(&d->arena, (size_t)n * sizeof(FcPoint));
    if (!p) return NULL;
    memcpy(p, pts, (size_t)n * sizeof(FcPoint));
    return p;
}

static int parse_xy_list(const char **tok, int ntok, int start, int last_is_layer, FcPoint *pts,
                         int maxn, int *n_out, int *layer_out, int *ok) {
    int end = last_is_layer ? ntok - 1 : ntok;
    int i = start;
    int n = 0;
    *ok = 1;
    while (i + 1 < end && n < maxn) {
        double x, y;
        if (!fc_parse_double(tok[i], &x) || !fc_parse_double(tok[i + 1], &y)) {
            *ok = 0;
            return 0;
        }
        pts[n].x = fc_clamp_coord(x);
        pts[n].y = fc_clamp_coord(y);
        n++;
        i += 2;
    }
    if (n >= maxn && i + 1 < end) {
        /* truncated */
    }
    *n_out = n;
    if (last_is_layer && ntok > start) *layer_out = tok_layer(tok[ntok - 1]);
    else *layer_out = 0;
    return 1;
}

static int parse_primitive(FcDrawing *d, FcPrim *p, const char **tok, int ntok, int line) {
    FcKind kind;
    int filled = 0;
    if (!fc_code_kind(tok[0], &kind, &filled)) return 0;
    fc_prim_init(p);
    p->kind = kind;
    p->filled = filled;
    p->src_line = line;

    switch (kind) {
    case FC_LINE:
        if (ntok < 5) return 0;
        p->u.line.a.x = tok_coord(tok[1]);
        p->u.line.a.y = tok_coord(tok[2]);
        p->u.line.b.x = tok_coord(tok[3]);
        p->u.line.b.y = tok_coord(tok[4]);
        p->layer = ntok > 5 ? tok_layer(tok[5]) : 0;
        fc_label_default(&p->name, p->u.line.a.x + 5, p->u.line.a.y + 5, p->layer);
        fc_label_default(&p->value, p->u.line.a.x + 5, p->u.line.a.y + 10, p->layer);
        return 1;
    case FC_BEZIER:
        if (ntok < 9) return 0;
        {
            int i;
            for (i = 0; i < 4; i++) {
                p->u.bezier.p[i].x = tok_coord(tok[1 + i * 2]);
                p->u.bezier.p[i].y = tok_coord(tok[2 + i * 2]);
            }
        }
        p->layer = ntok > 9 ? tok_layer(tok[9]) : 0;
        fc_label_default(&p->name, p->u.bezier.p[0].x + 5, p->u.bezier.p[0].y + 5, p->layer);
        fc_label_default(&p->value, p->u.bezier.p[0].x + 5, p->u.bezier.p[0].y + 10, p->layer);
        return 1;
    case FC_RECT:
    case FC_OVAL:
        if (ntok < 5) return 0;
        p->u.box.a.x = tok_coord(tok[1]);
        p->u.box.a.y = tok_coord(tok[2]);
        p->u.box.b.x = tok_coord(tok[3]);
        p->u.box.b.y = tok_coord(tok[4]);
        p->layer = ntok > 5 ? tok_layer(tok[5]) : 0;
        fc_label_default(&p->name, p->u.box.a.x + 5, p->u.box.a.y + 5, p->layer);
        fc_label_default(&p->value, p->u.box.a.x + 5, p->u.box.a.y + 10, p->layer);
        return 1;
    case FC_POLY: {
        FcPoint tmp[FC_MAX_VERTICES];
        int n = 0, layer = 0, ok = 1;
        if (ntok < 6) return 0;
        parse_xy_list(tok, ntok, 1, 1, tmp, FC_MAX_VERTICES, &n, &layer, &ok);
        if (!ok || n < 2) return 0;
        p->u.poly.pts = copy_pts(d, tmp, n);
        p->u.poly.n = n;
        p->layer = layer;
        fc_label_default(&p->name, tmp[0].x + 5, tmp[0].y + 5, layer);
        fc_label_default(&p->value, tmp[0].x + 5, tmp[0].y + 10, layer);
        return 1;
    }
    case FC_CURVE: {
        FcPoint tmp[FC_MAX_VERTICES];
        int n = 0, layer = 0, ok = 1;
        if (ntok < 6) return 0;
        p->u.curve.closed = tok_int(tok[1], 0) ? 1 : 0;
        parse_xy_list(tok, ntok, 2, 1, tmp, FC_MAX_VERTICES, &n, &layer, &ok);
        if (!ok || n < 1) return 0;
        p->u.curve.pts = copy_pts(d, tmp, n);
        p->u.curve.n = n;
        p->layer = layer;
        fc_label_default(&p->name, tmp[0].x + 5, tmp[0].y + 5, layer);
        fc_label_default(&p->value, tmp[0].x + 5, tmp[0].y + 10, layer);
        return 1;
    }
    case FC_CONN:
        if (ntok < 3) return 0;
        p->u.conn.p.x = tok_coord(tok[1]);
        p->u.conn.p.y = tok_coord(tok[2]);
        p->layer = ntok > 3 ? tok_layer(tok[3]) : 0;
        fc_label_default(&p->name, p->u.conn.p.x + 5, p->u.conn.p.y + 5, p->layer);
        fc_label_default(&p->value, p->u.conn.p.x + 5, p->u.conn.p.y + 10, p->layer);
        return 1;
    case FC_PCB_LINE:
        if (ntok < 6) return 0;
        p->u.pcb_line.a.x = tok_coord(tok[1]);
        p->u.pcb_line.a.y = tok_coord(tok[2]);
        p->u.pcb_line.b.x = tok_coord(tok[3]);
        p->u.pcb_line.b.y = tok_coord(tok[4]);
        p->u.pcb_line.width = tok_dbl(tok[5], 1);
        p->layer = ntok > 6 ? tok_layer(tok[6]) : 0;
        fc_label_default(&p->name, p->u.pcb_line.a.x + 5, p->u.pcb_line.a.y + 5, p->layer);
        fc_label_default(&p->value, p->u.pcb_line.a.x + 5, p->u.pcb_line.a.y + 10, p->layer);
        return 1;
    case FC_PCB_PAD:
        if (ntok < 7) return 0;
        p->u.pad.p.x = tok_coord(tok[1]);
        p->u.pad.p.y = tok_coord(tok[2]);
        p->u.pad.rx = tok_dbl(tok[3], 0);
        p->u.pad.ry = tok_dbl(tok[4], 0);
        p->u.pad.hole = tok_dbl(tok[5], 0);
        p->u.pad.style = tok_int(tok[6], 0);
        p->layer = ntok > 7 ? tok_layer(tok[7]) : 0;
        fc_label_default(&p->name, p->u.pad.p.x + 5, p->u.pad.p.y + 5, p->layer);
        fc_label_default(&p->value, p->u.pad.p.x + 5, p->u.pad.p.y + 10, p->layer);
        return 1;
    case FC_MACRO: {
        if (ntok < 6) return 0;
        p->u.macro.p.x = tok_coord(tok[1]);
        p->u.macro.p.y = tok_coord(tok[2]);
        p->u.macro.orient = tok_int(tok[3], 0) & 3;
        p->u.macro.mirror = tok_int(tok[4], 0) ? 1 : 0;
        {
            char *key = join_rest(&d->arena, tok, ntok, 5);
            fc_tolower_buf(key);
            p->u.macro.key = key;
        }
        p->layer = 0;
        fc_label_default(&p->name, p->u.macro.p.x + 10, p->u.macro.p.y + 10, 0);
        fc_label_default(&p->value, p->u.macro.p.x + 10, p->u.macro.p.y + 5, 0);
        return 1;
    }
    case FC_TEXT:
        return 0; /* handled as TY/TE */
    }
    return 0;
}

static void commit(ParseCtx *cx) {
    FcPrim *dst;
    if (!cx->has_pend) return;
    dst = fc_drawing_push(cx->d);
    if (!dst) return;
    *dst = cx->pend;
    cx->has_pend = 0;
    cx->expect_fcj = 0;
    cx->labels_left = 0;
}

static void apply_fcj(ParseCtx *cx, const char **tok, int ntok) {
    FcPrim *p = &cx->pend;
    int text_flag = 0;
    if (!cx->has_pend) return;
    cx->expect_fcj = 0;
    if (fc_kind_direct_labels(p->kind)) {
        cx->labels_left = 2;
        return;
    }
    if (fc_kind_arrowed(p->kind)) {
        if (ntok >= 7) {
            p->ext.arrows = tok_int(tok[1], 0);
            p->ext.arrow_style = tok_int(tok[2], 0);
            p->ext.arrow_len = tok_dbl(tok[3], 3);
            p->ext.arrow_half_w = tok_dbl(tok[4], 1);
            p->ext.dash = fc_clamp_dash(tok_int(tok[5], 0));
            text_flag = tok_int(tok[6], 0);
            p->has_ext = 1;
        } else if (ntok >= 3) {
            p->ext.dash = fc_clamp_dash(tok_int(tok[1], 0));
            text_flag = tok_int(tok[2], 0);
            p->has_ext = 1;
        }
    } else if (fc_kind_uses_fcj(p->kind)) {
        if (ntok >= 3) {
            p->ext.dash = fc_clamp_dash(tok_int(tok[1], 0));
            text_flag = tok_int(tok[2], 0);
            p->has_ext = 1;
        }
    }
    if (text_flag) cx->labels_left = 2;
}

static void attach_label(ParseCtx *cx, const FcLabel *l) {
    if (!cx->pend.has_name) {
        cx->pend.name = *l;
        cx->pend.has_name = 1;
    } else {
        cx->pend.value = *l;
        cx->pend.has_value = 1;
    }
    if (cx->labels_left > 0) cx->labels_left--;
    if (cx->labels_left == 0) commit(cx);
}

static void apply_fjc(ParseCtx *cx, const char **tok, int ntok) {
    FcDrawing *d = cx->d;
    if (ntok < 2) return;
    if (fc_streq(tok[1], "C") && ntok >= 3) {
        double v = tok_dbl(tok[2], 0);
        if (v > 0) d->cfg.diameter_conn = v;
    } else if (fc_streq(tok[1], "A") && ntok >= 3) {
        double v = tok_dbl(tok[2], 0);
        if (v > 0) d->cfg.line_width = v;
    } else if (fc_streq(tok[1], "B") && ntok >= 3) {
        double v = tok_dbl(tok[2], 0);
        if (v > 0) d->cfg.line_width_circles = v;
    } else if (fc_streq(tok[1], "L") && ntok >= 5) {
        int layer = tok_int(tok[2], -1);
        int rgb = tok_int(tok[3], 0);
        double alpha = tok_dbl(tok[4], 1);
        if (layer >= 0 && layer < FC_MAX_LAYERS) {
            d->layers[layer].rgb = (uint32_t)rgb & 0xFFFFFFu;
            d->layers[layer].alpha = (float)alpha;
            d->layers[layer].modified = 1;
        }
    } else if (fc_streq(tok[1], "N") && ntok >= 4) {
        int layer = tok_int(tok[2], -1);
        if (layer >= 0 && layer < FC_MAX_LAYERS) {
            char *name = join_rest(&d->arena, tok, ntok, 3);
            snprintf(d->layers[layer].name, sizeof d->layers[layer].name, "%s", name ? name : "");
            d->layers[layer].modified = 1;
        }
    } else if (fc_streq(tok[1], "IMG") && ntok >= 6) {
        d->cfg.has_img = 1;
        d->cfg.img_x = tok_dbl(tok[2], 0);
        d->cfg.img_y = tok_dbl(tok[3], 0);
        d->cfg.img_scale = tok_dbl(tok[4], 1);
        d->cfg.img_alpha = tok_dbl(tok[5], 1);
    }
}

static int tokenize(char *line, const char **tok, int max, int *truncated) {
    int n = 0;
    char *p = line;
    *truncated = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == 0) break;
        if (n >= max) {
            *truncated = 1;
            break;
        }
        tok[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p == ' ') {
            *p = 0;
            p++;
        }
    }
    return n;
}

static void handle_line(ParseCtx *cx, const char **tok, int ntok) {
    FcKind kind;
    int filled = 0;
    int is_ty, is_te, is_fcj, is_fjc, is_prim;

    if (ntok == 0) return;
    is_ty = fc_streq(tok[0], "TY");
    is_te = fc_streq(tok[0], "TE");
    is_fcj = fc_streq(tok[0], "FCJ");
    is_fjc = fc_streq(tok[0], "FJC");
    is_prim = fc_code_kind(tok[0], &kind, &filled) && !is_ty && !is_te;

    if (cx->has_pend && cx->expect_fcj) {
        if (is_fcj) {
            apply_fcj(cx, tok, ntok);
            return;
        }
        cx->expect_fcj = 0;
        if (fc_kind_direct_labels(cx->pend.kind) && is_ty) cx->labels_left = 2;
    }

    if (cx->has_pend && cx->labels_left > 0) {
        if (is_ty) {
            FcLabel lab;
            memset(&lab, 0, sizeof lab);
            if (parse_label(cx->d, &lab, tok, ntok)) attach_label(cx, &lab);
            else {
                fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "malformed TY label");
                commit(cx);
            }
            return;
        }
        commit(cx);
    }

    if (cx->has_pend) commit(cx);

    if (is_fcj) {
        /* stray FCJ */
        return;
    }
    if (is_fjc) {
        apply_fjc(cx, tok, ntok);
        return;
    }
    if (is_ty) {
        FcPrim *dst;
        FcLabel lab;
        memset(&lab, 0, sizeof lab);
        if (!parse_label(cx->d, &lab, tok, ntok)) {
            fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "malformed TY");
            return;
        }
        dst = fc_drawing_push(cx->d);
        if (!dst) return;
        dst->kind = FC_TEXT;
        dst->layer = lab.layer;
        dst->src_line = cx->line;
        dst->u.text = lab;
        return;
    }
    if (is_te) {
        FcPrim *dst;
        FcLabel lab;
        memset(&lab, 0, sizeof lab);
        if (!parse_te(cx->d, &lab, tok, ntok)) {
            fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "malformed TE");
            return;
        }
        dst = fc_drawing_push(cx->d);
        if (!dst) return;
        dst->kind = FC_TEXT;
        dst->layer = lab.layer;
        dst->src_line = cx->line;
        dst->u.text = lab;
        return;
    }
    if (is_prim) {
        FcPrim tmp;
        if (!parse_primitive(cx->d, &tmp, tok, ntok, cx->line)) {
            fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "malformed %s", tok[0]);
            return;
        }
        cx->pend = tmp;
        cx->has_pend = 1;
        cx->expect_fcj = 1;
        cx->labels_left = 0;
        return;
    }
    /* unknown command — ignore ([FIDOCAD], comments, …) */
}

int fc_parse(FcDrawing *d, const char *text, size_t len) {
    ParseCtx cx;
    const char *p, *end;
    char *line = NULL;
    size_t line_cap = 0;

    memset(&cx, 0, sizeof cx);
    cx.d = d;
    if (!text) return FC_OK;
    if (len == 0) len = fc_strlen(text);
    p = text;
    end = text + len;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF)
        p += 3;

    cx.line = 0;
    while (p < end) {
        const char *start = p;
        size_t n;
        int truncated = 0;
        const char *tok[FC_MAX_TOKENS];
        int ntok;
        while (p < end && *p != '\n' && *p != '\r') p++;
        n = (size_t)(p - start);
        cx.line++;
        if (n + 1 > line_cap) {
            size_t ncap = n + 1 < 256 ? 256 : n + 1;
            char *nl = (char *)realloc(line, ncap);
            if (!nl) {
                free(line);
                return FC_ERR_NOMEM;
            }
            line = nl;
            line_cap = ncap;
        }
        memcpy(line, start, n);
        line[n] = 0;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;

        ntok = tokenize(line, tok, FC_MAX_TOKENS, &truncated);
        if (truncated)
            fc_drawing_diag(d, cx.line, FC_DIAG_WARN, "line truncated to %d tokens", FC_MAX_TOKENS);
        handle_line(&cx, tok, ntok);
    }
    commit(&cx);
    free(line);
    return FC_OK;
}

int fc_parse_file(FcDrawing *d, const char *path) {
    char *text = NULL;
    size_t len = 0;
    int rc = fc_read_file(path, &text, &len);
    if (rc != FC_OK) return rc;
    rc = fc_parse(d, text, len);
    free(text);
    return rc;
}
