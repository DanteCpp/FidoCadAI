#include "internal.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

struct FcArenaBlock {
    struct FcArenaBlock *next;
    size_t used, cap;
    char data[];
};

static const char *const k_layer_names[FC_MAX_LAYERS] = {
    "Circuit",        "Bottom copper", "Top copper", "Silkscreen",
    "Other 1",        "Other 2",       "Other 3",    "Other 4",
    "Other 5",        "Other 6",       "Other 7",    "Other 8",
    "Other 9",        "Other 10",      "Other 11",   "Other 12"};

static const uint32_t k_layer_rgb[FC_MAX_LAYERS] = {
    0x000000, 0x000080, 0xFF0000, 0x008080, 0xFFC800, 0x7FFF00, 0x00FFFF, 0x008000,
    0x9ACD32, 0xFF1493, 0xB59B0C, 0x0180FF, 0xE1E1E1, 0xA2A2A2, 0x5F5F5F, 0x000000};

static const float k_layer_alpha[FC_MAX_LAYERS] = {
    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
    1.00f, 1.00f, 1.00f, 1.00f, 0.95f, 0.90f, 0.90f, 1.00f};

const char *fc_layer_default_name(int i) {
    if (i < 0 || i >= FC_MAX_LAYERS) return "Circuit";
    return k_layer_names[i];
}
uint32_t fc_layer_default_rgb(int i) {
    if (i < 0 || i >= FC_MAX_LAYERS) return 0;
    return k_layer_rgb[i];
}
float fc_layer_default_alpha(int i) {
    if (i < 0 || i >= FC_MAX_LAYERS) return 1.0f;
    return k_layer_alpha[i];
}

size_t fc_strlen(const char *s) { return s ? strlen(s) : 0; }

int fc_streq(const char *a, const char *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

void fc_tolower_buf(char *s) {
    if (!s) return;
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
    }
}

uint32_t fc_hash(const char *s) {
    uint32_t h = 2166136261u;
    if (!s) return h;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

void fc_arena_init(FcArena *a, size_t cap) {
    a->blocks = a->cur = NULL;
    a->used = 0;
    (void)cap;
}

void fc_arena_free(FcArena *a) {
    struct FcArenaBlock *b = a->blocks;
    while (b) {
        struct FcArenaBlock *n = b->next;
        free(b);
        b = n;
    }
    a->blocks = a->cur = NULL;
    a->used = 0;
}

void *fc_arena_alloc(FcArena *a, size_t n) {
    size_t aligned = (n + (FC_ALIGN - 1)) & ~(size_t)(FC_ALIGN - 1);
    struct FcArenaBlock *b;
    void *p;
    if (aligned == 0) aligned = FC_ALIGN;
    b = a->cur;
    if (!b || b->used + aligned > b->cap) {
        size_t cap = 65536;
        if (aligned > cap) cap = aligned;
        b = (struct FcArenaBlock *)malloc(offsetof(struct FcArenaBlock, data) + cap);
        if (!b) return NULL;
        b->next = a->blocks;
        b->used = 0;
        b->cap = cap;
        a->blocks = b;
        a->cur = b;
    }
    p = b->data + b->used;
    b->used += aligned;
    a->used += aligned;
    return p;
}

char *fc_arena_strndup(FcArena *a, const char *s, size_t n) {
    char *d = (char *)fc_arena_alloc(a, n + 1);
    if (!d) return NULL;
    if (s && n) memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *fc_arena_strdup(FcArena *a, const char *s) {
    return fc_arena_strndup(a, s ? s : "", fc_strlen(s));
}

void fc_buf_init(FcBuf *b) {
    b->p = NULL;
    b->n = b->cap = 0;
}

void fc_buf_free(FcBuf *b) {
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

int fc_buf_reserve(FcBuf *b, size_t extra) {
    if (b->n + extra + 1 <= b->cap) return 0;
    size_t ncap = b->cap ? b->cap : 256;
    while (b->n + extra + 1 > ncap) ncap *= 2;
    char *np = (char *)realloc(b->p, ncap);
    if (!np) return -1;
    b->p = np;
    b->cap = ncap;
    return 0;
}

void fc_buf_putc(FcBuf *b, char c) {
    if (fc_buf_reserve(b, 1) != 0) return;
    b->p[b->n++] = c;
    b->p[b->n] = 0;
}

void fc_buf_putn(FcBuf *b, const char *s, size_t n) {
    if (!s || n == 0) return;
    if (fc_buf_reserve(b, n) != 0) return;
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

void fc_buf_puts(FcBuf *b, const char *s) { fc_buf_putn(b, s, fc_strlen(s)); }

void fc_buf_printf(FcBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list aq;
    va_copy(aq, ap);
    int need = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (need < 0) {
        va_end(ap);
        return;
    }
    if (fc_buf_reserve(b, (size_t)need) != 0) {
        va_end(ap);
        return;
    }
    vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
    b->n += (size_t)need;
    va_end(ap);
}

char *fc_buf_steal(FcBuf *b) {
    char *p = b->p;
    if (!p) {
        p = (char *)malloc(1);
        if (p) p[0] = 0;
    }
    b->p = NULL;
    b->n = b->cap = 0;
    return p;
}

int fc_parse_int(const char *s, int *out) {
    double d;
    if (!fc_parse_double(s, &d)) return 0;
    if (d > 2147483647.0) d = 2147483647.0;
    if (d < -2147483648.0) d = -2147483648.0;
    *out = (int)d;
    return 1;
}

int fc_parse_double(const char *s, double *out) {
    if (!s || !*s) return 0;
    const char *p = s;
    int neg = 0;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    if (*p != '.' && (*p < '0' || *p > '9')) return 0;
    double v = 0.0;
    int any = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10.0 + (*p - '0');
        p++;
        any = 1;
    }
    if (*p == '.') {
        p++;
        double place = 0.1;
        while (*p >= '0' && *p <= '9') {
            v += (*p - '0') * place;
            place *= 0.1;
            p++;
            any = 1;
        }
    }
    if (!any || *p != 0) return 0;
    *out = neg ? -v : v;
    return 1;
}

double fc_clamp_coord(double v) {
    if (v < FC_MIN_COORD) return FC_MIN_COORD;
    if (v > FC_MAX_COORD) return FC_MAX_COORD;
    return v;
}

int fc_clamp_layer(int l) {
    if (l < 0 || l >= FC_MAX_LAYERS) return 0;
    return l;
}

int fc_clamp_dash(int d) {
    if (d < 0) return 0;
    if (d > 4) return 4;
    return d;
}

void fc_fmt_num(char *buf, size_t cap, double v) {
    if (!buf || cap == 0) return;
    if (!isfinite(v)) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    int neg = v < 0;
    if (neg) v = -v;
    /* keep v * 1000 inside long long; parsed values are clamped far below this */
    if (v > FC_MAX_NUM) v = FC_MAX_NUM;
    long long milli = (long long)(v * 1000.0 + 0.5);
    if (milli == 0) neg = 0;
    long long ip = milli / 1000;
    int frac = (int)(milli % 1000);
    char tmp[64];
    int n = 0;
    if (neg) tmp[n++] = '-';
    if (ip == 0) {
        tmp[n++] = '0';
    } else {
        char digs[32];
        int nd = 0;
        long long x = ip;
        while (x && nd < 32) {
            digs[nd++] = (char)('0' + (x % 10));
            x /= 10;
        }
        while (nd--) tmp[n++] = digs[nd];
    }
    if (frac) {
        tmp[n++] = '.';
        tmp[n++] = (char)('0' + (frac / 100));
        int r = frac % 100;
        if (r) {
            tmp[n++] = (char)('0' + (r / 10));
            if (r % 10) tmp[n++] = (char)('0' + (r % 10));
        }
    }
    tmp[n] = 0;
    if ((size_t)n >= cap) n = (int)cap - 1;
    memcpy(buf, tmp, (size_t)n + 1);
}

void fc_fmt_coord(char *buf, size_t cap, double v) { fc_fmt_num(buf, cap, v); }

int fc_read_file(const char *path, char **out, size_t *len) {
    FILE *fp;
    if (!path || fc_streq(path, "-")) {
        FcBuf b;
        fc_buf_init(&b);
        char tmp[8192];
        size_t n;
        while ((n = fread(tmp, 1, sizeof tmp, stdin)) > 0) fc_buf_putn(&b, tmp, n);
        if (ferror(stdin)) {
            fc_buf_free(&b);
            return FC_ERR_IO;
        }
        *len = b.n;
        *out = fc_buf_steal(&b);
        return *out ? FC_OK : FC_ERR_NOMEM;
    }
    fp = fopen(path, "rb");
    if (!fp) return FC_ERR_IO;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return FC_ERR_IO;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return FC_ERR_IO;
    }
    rewind(fp);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return FC_ERR_NOMEM;
    }
    size_t got = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[got] = 0;
    *out = buf;
    *len = got;
    return FC_OK;
}

int fc_write_file(const char *path, const char *data, size_t n) {
    FILE *fp;
    if (!path || fc_streq(path, "-")) {
        if (fwrite(data, 1, n, stdout) != n) return FC_ERR_IO;
        return FC_OK;
    }
    fp = fopen(path, "wb");
    if (!fp) return FC_ERR_IO;
    size_t w = fwrite(data, 1, n, fp);
    int err = ferror(fp);
    fclose(fp);
    return (w == n && !err) ? FC_OK : FC_ERR_IO;
}

void fc_config_init(FcConfig *c) {
    c->diameter_conn = 2.0;
    c->line_width = 0.5;
    c->line_width_circles = 0.35;
    c->has_img = 0;
    c->img_x = c->img_y = 0;
    c->img_scale = 1;
    c->img_alpha = 1;
}

void fc_layers_init(FcLayer layers[FC_MAX_LAYERS]) {
    int i;
    for (i = 0; i < FC_MAX_LAYERS; i++) {
        snprintf(layers[i].name, sizeof layers[i].name, "%s", k_layer_names[i]);
        layers[i].rgb = k_layer_rgb[i];
        layers[i].alpha = k_layer_alpha[i];
        layers[i].modified = 0;
    }
}

void fc_drawing_init(FcDrawing *d) {
    memset(d, 0, sizeof *d);
    fc_arena_init(&d->arena, 65536);
    fc_config_init(&d->cfg);
    fc_layers_init(d->layers);
}

void fc_drawing_free(FcDrawing *d) {
    free(d->prims);
    free(d->diags);
    fc_arena_free(&d->arena);
    memset(d, 0, sizeof *d);
}

void fc_drawing_clear(FcDrawing *d) {
    free(d->prims);
    free(d->diags);
    fc_arena_free(&d->arena);
    fc_drawing_init(d);
}

FcPrim *fc_drawing_push(FcDrawing *d) {
    if (d->nprims >= d->cap_prims) {
        int ncap = FC_GROW(d->cap_prims);
        FcPrim *np = (FcPrim *)realloc(d->prims, (size_t)ncap * sizeof(FcPrim));
        if (!np) return NULL;
        d->prims = np;
        d->cap_prims = ncap;
    }
    FcPrim *p = &d->prims[d->nprims++];
    fc_prim_init(p);
    return p;
}

void fc_drawing_diag(FcDrawing *d, int line, FcDiagLevel level, const char *fmt, ...) {
    if (d->ndiags >= d->cap_diags) {
        int ncap = FC_GROW(d->cap_diags);
        FcDiag *np = (FcDiag *)realloc(d->diags, (size_t)ncap * sizeof(FcDiag));
        if (!np) return;
        d->diags = np;
        d->cap_diags = ncap;
    }
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    FcDiag *g = &d->diags[d->ndiags++];
    g->line = line;
    g->level = level;
    g->msg = fc_arena_strdup(&d->arena, tmp);
}

void fc_prim_init(FcPrim *p) {
    memset(p, 0, sizeof *p);
    p->ext.arrow_len = 3.0;
    p->ext.arrow_half_w = 1.0;
}

void fc_label_default(FcLabel *l, double x, double y, int layer) {
    l->p.x = x;
    l->p.y = y;
    l->size_y = 4;
    l->size_x = 3;
    l->rotation = 0;
    l->style = 0;
    l->layer = layer;
    l->font = "*";
    l->text = "";
}

int fc_kind_uses_fcj(FcKind k) {
    return k == FC_LINE || k == FC_BEZIER || k == FC_RECT || k == FC_OVAL || k == FC_POLY ||
           k == FC_CURVE;
}

int fc_kind_direct_labels(FcKind k) {
    return k == FC_CONN || k == FC_PCB_LINE || k == FC_PCB_PAD || k == FC_MACRO;
}

int fc_kind_arrowed(FcKind k) { return k == FC_LINE || k == FC_BEZIER || k == FC_CURVE; }

int fc_code_kind(const char *tok, FcKind *kind, int *filled) {
    if (!tok || !tok[0] || !tok[1]) return 0;
    if (tok[2] != 0) return 0;
    *filled = 0;
    char a = tok[0], b = tok[1];
    if (a == 'L' && b == 'I') {
        *kind = FC_LINE;
        return 1;
    }
    if (a == 'B' && b == 'E') {
        *kind = FC_BEZIER;
        return 1;
    }
    if (a == 'R' && b == 'V') {
        *kind = FC_RECT;
        return 1;
    }
    if (a == 'R' && b == 'P') {
        *kind = FC_RECT;
        *filled = 1;
        return 1;
    }
    if (a == 'E' && b == 'V') {
        *kind = FC_OVAL;
        return 1;
    }
    if (a == 'E' && b == 'P') {
        *kind = FC_OVAL;
        *filled = 1;
        return 1;
    }
    if (a == 'P' && b == 'V') {
        *kind = FC_POLY;
        return 1;
    }
    if (a == 'P' && b == 'P') {
        *kind = FC_POLY;
        *filled = 1;
        return 1;
    }
    if (a == 'C' && b == 'V') {
        *kind = FC_CURVE;
        return 1;
    }
    if (a == 'C' && b == 'P') {
        *kind = FC_CURVE;
        *filled = 1;
        return 1;
    }
    if (a == 'S' && b == 'A') {
        *kind = FC_CONN;
        return 1;
    }
    if (a == 'P' && b == 'L') {
        *kind = FC_PCB_LINE;
        return 1;
    }
    if (a == 'P' && b == 'A') {
        *kind = FC_PCB_PAD;
        return 1;
    }
    if (a == 'M' && b == 'C') {
        *kind = FC_MACRO;
        return 1;
    }
    if (a == 'T' && b == 'Y') {
        *kind = FC_TEXT;
        return 1;
    }
    if (a == 'T' && b == 'E') {
        *kind = FC_TEXT;
        return 1;
    }
    return 0;
}

const char *fc_kind_name(FcKind k) {
    switch (k) {
    case FC_LINE:
        return "line";
    case FC_BEZIER:
        return "bezier";
    case FC_RECT:
        return "rectangle";
    case FC_OVAL:
        return "ellipse";
    case FC_POLY:
        return "polygon";
    case FC_CURVE:
        return "curve";
    case FC_CONN:
        return "connection";
    case FC_PCB_LINE:
        return "pcb_track";
    case FC_PCB_PAD:
        return "pcb_pad";
    case FC_MACRO:
        return "macro";
    case FC_TEXT:
        return "text";
    default:
        return "unknown";
    }
}

const char *fc_kind_code(const FcPrim *p) {
    switch (p->kind) {
    case FC_LINE:
        return "LI";
    case FC_BEZIER:
        return "BE";
    case FC_RECT:
        return p->filled ? "RP" : "RV";
    case FC_OVAL:
        return p->filled ? "EP" : "EV";
    case FC_POLY:
        return p->filled ? "PP" : "PV";
    case FC_CURVE:
        return p->filled ? "CP" : "CV";
    case FC_CONN:
        return "SA";
    case FC_PCB_LINE:
        return "PL";
    case FC_PCB_PAD:
        return "PA";
    case FC_MACRO:
        return "MC";
    case FC_TEXT:
        return "TY";
    default:
        return "??";
    }
}

void fc_json_escape(FcBuf *b, const char *s) {
    fc_buf_putc(b, '"');
    if (!s) {
        fc_buf_putc(b, '"');
        return;
    }
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            fc_buf_putc(b, '\\');
            fc_buf_putc(b, (char)c);
        } else if (c == '\n') {
            fc_buf_puts(b, "\\n");
        } else if (c == '\r') {
            fc_buf_puts(b, "\\r");
        } else if (c == '\t') {
            fc_buf_puts(b, "\\t");
        } else if (c < 0x20) {
            fc_buf_printf(b, "\\u%04x", c);
        } else {
            fc_buf_putc(b, (char)c);
        }
    }
    fc_buf_putc(b, '"');
}

void fc_xform_point(FcPoint *pt, int orient, int mirror, double ox, double oy) {
    double xc = pt->x - 100.0;
    double yc = pt->y - 100.0;
    double vx = 0, vy = 0;
    orient &= 3;
    if (mirror) {
        switch (orient) {
        case 0:
            vx = -xc;
            vy = yc;
            break;
        case 1:
            vx = yc;
            vy = xc;
            break;
        case 2:
            vx = xc;
            vy = -yc;
            break;
        default:
            vx = -yc;
            vy = -xc;
            break;
        }
    } else {
        switch (orient) {
        case 0:
            vx = xc;
            vy = yc;
            break;
        case 1:
            vx = -yc;
            vy = xc;
            break;
        case 2:
            vx = -xc;
            vy = -yc;
            break;
        default:
            vx = yc;
            vy = -xc;
            break;
        }
    }
    pt->x = fc_clamp_coord(vx + ox);
    pt->y = fc_clamp_coord(vy + oy);
}

void fc_xform_label(FcLabel *l, int orient, int mirror, double ox, double oy) {
    /* The renderer draws text as R(rotation) * M^style_mirror, while points map as
     * M^mirror * R(90 * orient). Composing them: a mirror flips the text's mirror bit
     * and negates its total rotation (M * R(t) == R(-t) * M). */
    int rot = l->rotation + (orient & 3) * 90;
    fc_xform_point(&l->p, orient, mirror, ox, oy);
    if (mirror) {
        rot = -rot;
        l->style ^= FC_STYLE_MIRROR;
    }
    rot %= 360;
    if (rot < 0) rot += 360;
    l->rotation = rot;
}

void fc_xform_prim(FcPrim *p, int orient, int mirror, double ox, double oy) {
    int i;
    switch (p->kind) {
    case FC_LINE:
        fc_xform_point(&p->u.line.a, orient, mirror, ox, oy);
        fc_xform_point(&p->u.line.b, orient, mirror, ox, oy);
        break;
    case FC_BEZIER:
        for (i = 0; i < 4; i++) fc_xform_point(&p->u.bezier.p[i], orient, mirror, ox, oy);
        break;
    case FC_RECT:
    case FC_OVAL:
        fc_xform_point(&p->u.box.a, orient, mirror, ox, oy);
        fc_xform_point(&p->u.box.b, orient, mirror, ox, oy);
        break;
    case FC_POLY:
        for (i = 0; i < p->u.poly.n; i++) fc_xform_point(&p->u.poly.pts[i], orient, mirror, ox, oy);
        break;
    case FC_CURVE:
        for (i = 0; i < p->u.curve.n; i++)
            fc_xform_point(&p->u.curve.pts[i], orient, mirror, ox, oy);
        break;
    case FC_CONN:
        fc_xform_point(&p->u.conn.p, orient, mirror, ox, oy);
        break;
    case FC_PCB_LINE:
        fc_xform_point(&p->u.pcb_line.a, orient, mirror, ox, oy);
        fc_xform_point(&p->u.pcb_line.b, orient, mirror, ox, oy);
        break;
    case FC_PCB_PAD:
        fc_xform_point(&p->u.pad.p, orient, mirror, ox, oy);
        break;
    case FC_MACRO:
        /* outer M^mo R^oo applied to inner M^mi R^oi: if the inner one is mirrored,
         * R^oo * M == M * R^-oo, so the rotations subtract */
        fc_xform_point(&p->u.macro.p, orient, mirror, ox, oy);
        if (p->u.macro.mirror)
            p->u.macro.orient = (p->u.macro.orient - orient) & 3;
        else
            p->u.macro.orient = (p->u.macro.orient + orient) & 3;
        if (mirror) p->u.macro.mirror = !p->u.macro.mirror;
        break;
    case FC_TEXT:
        fc_xform_label(&p->u.text, orient, mirror, ox, oy);
        break;
    }
    if (p->has_name) fc_xform_label(&p->name, orient, mirror, ox, oy);
    if (p->has_value) fc_xform_label(&p->value, orient, mirror, ox, oy);
}
