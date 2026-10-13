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

static const FcLayer k_layers[FC_MAX_LAYERS] = {
    {"Circuit", 0x000000, 1.00f, 0},  {"Bottom copper", 0x000080, 1.00f, 0},
    {"Top copper", 0xFF0000, 1.00f, 0}, {"Silkscreen", 0x008080, 1.00f, 0},
    {"Other 1", 0xFFC800, 1.00f, 0},  {"Other 2", 0x7FFF00, 1.00f, 0},
    {"Other 3", 0x00FFFF, 1.00f, 0},  {"Other 4", 0x008000, 1.00f, 0},
    {"Other 5", 0x9ACD32, 1.00f, 0},  {"Other 6", 0xFF1493, 1.00f, 0},
    {"Other 7", 0xB59B0C, 1.00f, 0},  {"Other 8", 0x0180FF, 1.00f, 0},
    {"Other 9", 0xE1E1E1, 0.95f, 0},  {"Other 10", 0xA2A2A2, 0.90f, 0},
    {"Other 11", 0x5F5F5F, 0.90f, 0}, {"Other 12", 0x000000, 1.00f, 0}};

const FcLayer *fc_default_layer(int i) { return &k_layers[fc_clamp_layer(i)]; }

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

int fc_parse_bool(const char *v) {
    return !(fc_streq(v, "0") || fc_streq(v, "false") || fc_streq(v, "no") ||
             fc_streq(v, "off") || fc_streq(v, ""));
}

int fc_env_flag(const char *name, int def) {
    const char *v = getenv(name);
    return v ? fc_parse_bool(v) : def;
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

void fc_layers_init(FcLayer layers[FC_MAX_LAYERS]) { memcpy(layers, k_layers, sizeof k_layers); }

void fc_drawing_init(FcDrawing *d) {
    memset(d, 0, sizeof *d);
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

/* Everything that differs per kind in the file format; npts 0 = vertex list. */
static const struct {
    const char *name, *code, *filled_code;
    int npts, caps;
} k_kinds[] = {
    [FC_LINE] = {"line", "LI", NULL, 2, FC_KIND_DASH | FC_KIND_ARROW},
    [FC_BEZIER] = {"bezier", "BE", NULL, 4, FC_KIND_DASH | FC_KIND_ARROW},
    [FC_RECT] = {"rectangle", "RV", "RP", 2, FC_KIND_DASH | FC_KIND_FILL},
    [FC_OVAL] = {"ellipse", "EV", "EP", 2, FC_KIND_DASH | FC_KIND_FILL},
    [FC_POLY] = {"polygon", "PV", "PP", 0, FC_KIND_DASH | FC_KIND_FILL},
    [FC_CURVE] = {"curve", "CV", "CP", 0, FC_KIND_DASH | FC_KIND_ARROW | FC_KIND_FILL},
    [FC_CONN] = {"connection", "SA", NULL, 1, FC_KIND_LABELS},
    [FC_PCB_LINE] = {"pcb_track", "PL", NULL, 2, FC_KIND_LABELS},
    [FC_PCB_PAD] = {"pcb_pad", "PA", NULL, 1, FC_KIND_LABELS},
    [FC_MACRO] = {"macro", "MC", NULL, 1, FC_KIND_LABELS},
    [FC_TEXT] = {"text", "TY", NULL, 1, 0},
};
#define KIND_OK(k) ((unsigned)(k) <= (unsigned)FC_TEXT)

/* fc_prim_points relies on every fixed-size member starting with its points. */
_Static_assert(offsetof(FcPrim, u.line.b) == offsetof(FcPrim, u) + sizeof(FcPoint), "line");
_Static_assert(offsetof(FcPrim, u.box.b) == offsetof(FcPrim, u) + sizeof(FcPoint), "box");
_Static_assert(offsetof(FcPrim, u.pcb_line.b) == offsetof(FcPrim, u) + sizeof(FcPoint), "pcb");
_Static_assert(offsetof(FcPrim, u.bezier.p) == offsetof(FcPrim, u), "bezier");
_Static_assert(offsetof(FcPrim, u.conn.p) == offsetof(FcPrim, u), "conn");
_Static_assert(offsetof(FcPrim, u.pad.p) == offsetof(FcPrim, u), "pad");
_Static_assert(offsetof(FcPrim, u.macro.p) == offsetof(FcPrim, u), "macro");
_Static_assert(offsetof(FcPrim, u.text.p) == offsetof(FcPrim, u), "text");

int fc_kind_caps(FcKind k) { return KIND_OK(k) ? k_kinds[k].caps : 0; }

FcPoint *fc_prim_points(const FcPrim *p, int *n) {
    FcPrim *q = (FcPrim *)p;
    *n = KIND_OK(p->kind) ? k_kinds[p->kind].npts : 0;
    if (*n || !KIND_OK(p->kind)) return (FcPoint *)&q->u;
    *n = q->u.poly.n;
    return q->u.poly.pts;
}

int fc_code_kind(const char *tok, FcKind *kind, int *filled) {
    int k;
    if (!tok) return 0;
    for (k = 0; k <= FC_TEXT; k++) {
        *filled = fc_streq(tok, k_kinds[k].filled_code);
        if (*filled || fc_streq(tok, k_kinds[k].code)) {
            *kind = (FcKind)k;
            return 1;
        }
    }
    return 0;
}

const char *fc_kind_name(FcKind k) { return KIND_OK(k) ? k_kinds[k].name : "unknown"; }

const char *fc_kind_code(const FcPrim *p) {
    if (!KIND_OK(p->kind)) return "??";
    return p->filled && k_kinds[p->kind].filled_code ? k_kinds[p->kind].filled_code
                                                     : k_kinds[p->kind].code;
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

/* Macro placement: the body's origin (100,100) goes to (ox,oy), rotated by
 * orient quarter turns clockwise, then mirrored horizontally. */
void fc_xform_point(FcPoint *pt, int orient, int mirror, double ox, double oy) {
    double vx = pt->x - 100.0, vy = pt->y - 100.0, t;
    int k;
    for (k = 0; k < (orient & 3); k++) {
        t = vx;
        vx = -vy;
        vy = t;
    }
    if (mirror) vx = -vx;
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
    int i, n;
    FcPoint *pts = fc_prim_points(p, &n);
    if (p->kind == FC_TEXT) {
        fc_xform_label(&p->u.text, orient, mirror, ox, oy);
    } else {
        for (i = 0; i < n; i++) fc_xform_point(&pts[i], orient, mirror, ox, oy);
    }
    if (p->kind == FC_MACRO) {
        /* outer M^mo R^oo applied to inner M^mi R^oi: if the inner one is mirrored,
         * R^oo * M == M * R^-oo, so the rotations subtract */
        if (p->u.macro.mirror)
            p->u.macro.orient = (p->u.macro.orient - orient) & 3;
        else
            p->u.macro.orient = (p->u.macro.orient + orient) & 3;
        if (mirror) p->u.macro.mirror = !p->u.macro.mirror;
    }
    if (p->has_name) fc_xform_label(&p->name, orient, mirror, ox, oy);
    if (p->has_value) fc_xform_label(&p->value, orient, mirror, ox, oy);
}
