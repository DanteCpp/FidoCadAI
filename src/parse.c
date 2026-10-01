#include "internal.h"

typedef struct {
    FcDrawing *d;
    FcPrim pend;
    int has_pend;
    int expect_fcj;
    int labels_left;
    int labels_seen; /* slot of the next label: 0 = name, 1 = value */
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

/* sizes, widths, radii: same magnitude bound as coordinates so every emitter and
 * the rasterizer stay in range */
static double tok_dbl(const char *s, double def) {
    double v;
    if (!fc_parse_double(s, &v)) return def;
    if (v > FC_MAX_COORD) return FC_MAX_COORD;
    if (v < -FC_MAX_COORD) return -FC_MAX_COORD;
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

/* Parses x/y pairs from tok[start..] into the drawing arena. An odd token left
 * over is the layer (FidoCadJS PrimitivePolygon/ComplexCurve.parseTokens, so
 * "PV 105 95 105 115 120 105" keeps its last vertex). Returns the point count
 * (0 on a bad number or no memory). */
static int parse_xy_list(FcDrawing *d, const char **tok, int ntok, int start, int line,
                         FcPoint **pts_out, int *layer_out) {
    int npairs = (ntok - start) / 2;
    int n = npairs;
    int i;
    FcPoint *pts;
    if (n <= 0) return 0;
    if (n > FC_MAX_VERTICES) {
        fc_drawing_diag(d, line, FC_DIAG_WARN, "%s truncated to %d vertices", tok[0],
                        FC_MAX_VERTICES);
        n = FC_MAX_VERTICES;
    }
    pts = (FcPoint *)fc_arena_alloc(&d->arena, (size_t)n * sizeof(FcPoint));
    if (!pts) return 0;
    for (i = 0; i < n; i++) {
        double x, y;
        if (!fc_parse_double(tok[start + 2 * i], &x) ||
            !fc_parse_double(tok[start + 2 * i + 1], &y))
            return 0;
        pts[i].x = fc_clamp_coord(x);
        pts[i].y = fc_clamp_coord(y);
    }
    *pts_out = pts;
    *layer_out = (npairs * 2 < ntok - start) ? tok_layer(tok[start + 2 * npairs]) : 0;
    return n;
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
        FcPoint *pts = NULL;
        int n, layer = 0;
        if (ntok < 6) return 0;
        n = parse_xy_list(d, tok, ntok, 1, line, &pts, &layer);
        if (n < 2) return 0;
        p->u.poly.pts = pts;
        p->u.poly.n = n;
        p->layer = layer;
        fc_label_default(&p->name, pts[0].x + 5, pts[0].y + 5, layer);
        fc_label_default(&p->value, pts[0].x + 5, pts[0].y + 10, layer);
        return 1;
    }
    case FC_CURVE: {
        FcPoint *pts = NULL;
        int n, layer = 0;
        if (ntok < 6) return 0;
        p->u.curve.closed = tok_int(tok[1], 0) ? 1 : 0;
        n = parse_xy_list(d, tok, ntok, 2, line, &pts, &layer);
        if (n < 1) return 0;
        p->u.curve.pts = pts;
        p->u.curve.n = n;
        p->layer = layer;
        fc_label_default(&p->name, pts[0].x + 5, pts[0].y + 5, layer);
        fc_label_default(&p->value, pts[0].x + 5, pts[0].y + 10, layer);
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
            if (!key) return 0;
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

static double fcj_width(ParseCtx *cx, const char *s) {
    double w = tok_dbl(s, -1);
    if (w < 0) {
        fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "bad FCJ stroke width '%s'", s);
        return 0;
    }
    return w;
}

static void apply_fcj(ParseCtx *cx, const char **tok, int ntok) {
    FcPrim *p = &cx->pend;
    int text_flag = 0;
    if (!cx->has_pend) return;
    cx->expect_fcj = 0;
    if (fc_kind_direct_labels(p->kind)) {
        cx->labels_left = 2;
        cx->labels_seen = 0;
        return;
    }
    /* The optional stroke width sits between the dash style and the text flag:
     * FidoCadJS/FidoCadJ read the dash by position and the text flag as the last
     * token, so they ignore it and still attach the labels. */
    if (fc_kind_arrowed(p->kind) && ntok >= 7) {
        p->ext.arrows = tok_int(tok[1], 0);
        p->ext.arrow_style = tok_int(tok[2], 0);
        p->ext.arrow_len = tok_dbl(tok[3], 3);
        p->ext.arrow_half_w = tok_dbl(tok[4], 1);
        p->ext.dash = fc_clamp_dash(tok_int(tok[5], 0));
        if (ntok >= 8) p->ext.width = fcj_width(cx, tok[6]);
        text_flag = tok_int(tok[ntok >= 8 ? 7 : 6], 0);
        p->has_ext = 1;
    } else if (fc_kind_uses_fcj(p->kind) && ntok >= 3) {
        p->ext.dash = fc_clamp_dash(tok_int(tok[1], 0));
        if (ntok >= 4) p->ext.width = fcj_width(cx, tok[2]);
        text_flag = tok_int(tok[ntok >= 4 ? 3 : 2], 0);
        p->has_ext = 1;
    }
    if (text_flag) {
        cx->labels_left = 2;
        cx->labels_seen = 0;
    }
}

static void attach_label(ParseCtx *cx, const FcLabel *l) {
    /* The first TY line is the name slot, the second the value slot. An empty
     * text is an absent label, like FidoCadJS (name/value '' is no label). */
    int slot = cx->labels_seen++;
    if (l->text && l->text[0]) {
        if (slot == 0) {
            cx->pend.name = *l;
            cx->pend.has_name = 1;
        } else {
            cx->pend.value = *l;
            cx->pend.has_value = 1;
        }
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
        /* Like FidoCadJS ParserActions: labels on SA/PL/PA/MC are introduced by
         * an explicit bare `FCJ` line. A `TY` directly after the primitive is
         * standalone text, not its name/value. */
        cx->expect_fcj = 0;
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
    const char **tok;

    memset(&cx, 0, sizeof cx);
    cx.d = d;
    if (!text) return FC_OK;
    tok = (const char **)malloc(FC_MAX_TOKENS * sizeof *tok);
    if (!tok) return FC_ERR_NOMEM;
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
        int ntok;
        while (p < end && *p != '\n' && *p != '\r') p++;
        n = (size_t)(p - start);
        cx.line++;
        if (n + 1 > line_cap) {
            size_t ncap = n + 1 < 256 ? 256 : n + 1;
            char *nl = (char *)realloc(line, ncap);
            if (!nl) {
                free(line);
                free(tok);
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
    free(tok);
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
