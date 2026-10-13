#include "internal.h"

/* A primitive may be followed by FCJ, and a labelled FCJ by two TY lines
 * (name, then value); pend holds the primitive until nothing more can attach. */
typedef enum { IDLE, AFTER_PRIM, NAME_NEXT, VALUE_NEXT } ParseState;

typedef struct {
    FcDrawing *d;
    FcPrim pend; /* valid when state != IDLE */
    ParseState state;
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
    fc_label_default(l, tok_coord(tok[1]), tok_coord(tok[2]), 0);
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

/* name/value labels stay unset (has_name/has_value 0) until TY lines or an
 * edit attach them; only then are their fields meaningful. */
static int parse_primitive(FcDrawing *d, FcPrim *p, const char **tok, int ntok, int line) {
    FcKind kind;
    FcPoint *pts;
    int filled = 0, i, n, extra;
    if (!fc_code_kind(tok[0], &kind, &filled) || kind == FC_TEXT) return 0; /* TY/TE elsewhere */
    fc_prim_init(p);
    p->kind = kind;
    p->filled = filled;
    p->src_line = line;
    if (kind == FC_POLY || kind == FC_CURVE) {
        if (ntok < 6) return 0;
        if (kind == FC_CURVE) p->u.poly.closed = tok_int(tok[1], 0) ? 1 : 0;
        n = parse_xy_list(d, tok, ntok, kind == FC_CURVE ? 2 : 1, line, &p->u.poly.pts, &p->layer);
        p->u.poly.n = n;
        return n >= (kind == FC_CURVE ? 1 : 2);
    }
    /* fixed point count: coordinates, then the kind's own fields, then the layer */
    pts = fc_prim_points(p, &n);
    extra = kind == FC_PCB_LINE ? 1 : kind == FC_PCB_PAD ? 4 : kind == FC_MACRO ? 3 : 0;
    if (ntok < 1 + 2 * n + extra) return 0;
    for (i = 0; i < n; i++) {
        pts[i].x = tok_coord(tok[1 + 2 * i]);
        pts[i].y = tok_coord(tok[2 + 2 * i]);
    }
    i = 1 + 2 * n;
    if (kind == FC_PCB_LINE) {
        p->u.pcb_line.width = tok_dbl(tok[i++], 1);
    } else if (kind == FC_PCB_PAD) {
        p->u.pad.rx = tok_dbl(tok[i], 0);
        p->u.pad.ry = tok_dbl(tok[i + 1], 0);
        p->u.pad.hole = tok_dbl(tok[i + 2], 0);
        p->u.pad.style = tok_int(tok[i + 3], 0);
        i += 4;
    } else if (kind == FC_MACRO) {
        char *key = join_rest(&d->arena, tok, ntok, i + 2);
        if (!key) return 0;
        fc_tolower_buf(key);
        p->u.macro.orient = tok_int(tok[i], 0) & 3;
        p->u.macro.mirror = tok_int(tok[i + 1], 0) ? 1 : 0;
        p->u.macro.key = key;
        return 1; /* a macro has no layer of its own */
    }
    p->layer = ntok > i ? tok_layer(tok[i]) : 0;
    return 1;
}

static void commit(ParseCtx *cx) {
    FcPrim *dst;
    if (cx->state == IDLE) return;
    cx->state = IDLE;
    dst = fc_drawing_push(cx->d);
    if (dst) *dst = cx->pend;
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
    if (fc_kind_caps(p->kind) & FC_KIND_LABELS) {
        cx->state = NAME_NEXT;
        return;
    }
    /* The optional stroke width sits between the dash style and the text flag:
     * FidoCadJS/FidoCadJ read the dash by position and the text flag as the last
     * token, so they ignore it and still attach the labels. */
    if ((fc_kind_caps(p->kind) & FC_KIND_ARROW) && ntok >= 7) {
        p->ext.arrows = tok_int(tok[1], 0);
        p->ext.arrow_style = tok_int(tok[2], 0);
        p->ext.arrow_len = tok_dbl(tok[3], 3);
        p->ext.arrow_half_w = tok_dbl(tok[4], 1);
        p->ext.dash = fc_clamp_dash(tok_int(tok[5], 0));
        if (ntok >= 8) p->ext.width = fcj_width(cx, tok[6]);
        text_flag = tok_int(tok[ntok >= 8 ? 7 : 6], 0);
        p->has_ext = 1;
    } else if ((fc_kind_caps(p->kind) & FC_KIND_DASH) && ntok >= 3) {
        p->ext.dash = fc_clamp_dash(tok_int(tok[1], 0));
        if (ntok >= 4) p->ext.width = fcj_width(cx, tok[2]);
        text_flag = tok_int(tok[ntok >= 4 ? 3 : 2], 0);
        p->has_ext = 1;
    }
    if (text_flag) cx->state = NAME_NEXT;
    else commit(cx); /* no labels follow */
}

static void attach_label(ParseCtx *cx, const FcLabel *l) {
    /* An empty text is an absent label, like FidoCadJS (name/value '' is no label). */
    int value = cx->state == VALUE_NEXT;
    if (l->text && l->text[0]) {
        if (value) {
            cx->pend.value = *l;
            cx->pend.has_value = 1;
        } else {
            cx->pend.name = *l;
            cx->pend.has_name = 1;
        }
    }
    if (value) commit(cx);
    else cx->state = VALUE_NEXT;
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
    FcLabel lab;
    int filled, is_ty, is_te, is_fcj;

    if (ntok == 0) return;
    is_ty = fc_streq(tok[0], "TY");
    is_te = fc_streq(tok[0], "TE");
    is_fcj = fc_streq(tok[0], "FCJ");

    /* Like FidoCadJS ParserActions: labels on SA/PL/PA/MC are introduced by an
     * explicit bare `FCJ` line. A `TY` directly after the primitive is
     * standalone text, not its name/value. */
    if (cx->state == AFTER_PRIM && is_fcj) {
        apply_fcj(cx, tok, ntok);
        return;
    }
    if ((cx->state == NAME_NEXT || cx->state == VALUE_NEXT) && is_ty) {
        if (parse_label(cx->d, &lab, tok, ntok)) {
            attach_label(cx, &lab);
        } else {
            fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "malformed TY label");
            commit(cx);
        }
        return;
    }
    commit(cx);

    if (is_fcj) return; /* stray FCJ */
    if (fc_streq(tok[0], "FJC")) {
        apply_fjc(cx, tok, ntok);
        return;
    }
    if (is_ty || is_te) {
        FcPrim *dst;
        if (!(is_ty ? parse_label(cx->d, &lab, tok, ntok) : parse_te(cx->d, &lab, tok, ntok))) {
            fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "malformed %s", tok[0]);
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
    if (fc_code_kind(tok[0], &kind, &filled)) {
        if (parse_primitive(cx->d, &cx->pend, tok, ntok, cx->line))
            cx->state = AFTER_PRIM;
        else
            fc_drawing_diag(cx->d, cx->line, FC_DIAG_WARN, "malformed %s", tok[0]);
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
