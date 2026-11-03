/* Portable embedded images, decoded by stb_image (MIT/public domain).
 * Original encoded bytes are preserved; immutable decoded resources are shared
 * by undo reparses and expanded drawings. Cache ownership is bounded to 64 MiB. */
#include "internal.h"
#include <math.h>
#include <stdatomic.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#include "stb_image.h"

static atomic_flag image_lock = ATOMIC_FLAG_INIT;
static FcImage *cache[8];
static size_t cache_bytes;
static void lock(void) { while (atomic_flag_test_and_set(&image_lock)) {} }
static void unlock(void) { atomic_flag_clear(&image_lock); }
static void release(FcImage *im) {
    if (--im->refs) return;
    free(im->base64); stbi_image_free(im->rgba); free(im);
}
int fc_image_hold(FcDrawing *d, const FcImage *image) {
    if (!image) return 0;
    for (int i = 0; i < d->nimages; ++i) if (d->images[i] == image) return 1;
    if (d->nimages == d->cap_images) {
        int cap = FC_GROW(d->cap_images);
        FcImage **v = realloc(d->images, (size_t)cap * sizeof *v);
        if (!v) return 0;
        d->images = v; d->cap_images = cap;
    }
    lock(); ++((FcImage *)image)->refs; unlock();
    d->images[d->nimages++] = (FcImage *)image;
    return 1;
}
void fc_images_release(FcDrawing *d) {
    lock();
    for (int i = 0; i < d->nimages; ++i) release(d->images[i]);
    unlock();
    free(d->images); d->images = NULL; d->nimages = d->cap_images = 0;
}
void fc_image_size(const FcImage *im, int *w, int *h) {
    if (w) *w = im ? im->width : 0;
    if (h) *h = im ? im->height : 0;
}
const uint8_t *fc_image_pixels(const FcImage *im) { return im ? im->rgba : NULL; }
static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *encode(const unsigned char *p, size_t n) {
    char *s = malloc((n + 2) / 3 * 4 + 1), *q = s;
    if (!s) return NULL;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)p[i] << 16;
        if (i + 1 < n) v |= (unsigned)p[i+1] << 8;
        if (i + 2 < n) v |= p[i+2];
        *q++ = alphabet[v >> 18]; *q++ = alphabet[(v >> 12) & 63];
        *q++ = i + 1 < n ? alphabet[(v >> 6) & 63] : '=';
        *q++ = i + 2 < n ? alphabet[v & 63] : '=';
    }
    *q = 0; return s;
}
static int digit(char c) {
    const char *p = c ? strchr(alphabet, c) : NULL;
    return p ? (int)(p - alphabet) : -1;
}
static unsigned char *decode(const char *s, size_t *len) {
    size_t n = strlen(s), o = 0;
    if (!n || n % 4 || n > (FC_IMAGE_MAX_BYTES + 2) / 3 * 4) return NULL;
    unsigned char *p = malloc(n / 4 * 3);
    if (!p) return NULL;
    for (size_t i = 0; i < n; i += 4) {
        int a = digit(s[i]), b = digit(s[i+1]), c = digit(s[i+2]), d = digit(s[i+3]);
        int last = i + 4 == n, pad2 = s[i+2] == '=', pad1 = s[i+3] == '=';
        if (a < 0 || b < 0 || (c < 0 && !pad2) || (d < 0 && !pad1) ||
            ((pad1 || pad2) && !last) || (pad2 && !pad1) ||
            (pad2 && (b & 15)) || (pad1 && !pad2 && (c & 3))) { free(p); return NULL; }
        unsigned v = (unsigned)a << 18 | (unsigned)b << 12 |
                     (unsigned)(c < 0 ? 0 : c) << 6 | (unsigned)(d < 0 ? 0 : d);
        p[o++] = v >> 16;
        if (!pad2) p[o++] = v >> 8;
        if (!pad1) p[o++] = v;
    }
    if (o > FC_IMAGE_MAX_BYTES) { free(p); return NULL; }
    *len = o; return p;
}
/* Returns a retained resource. Decode outside the lock, then check again to
 * handle simultaneous import of the same image from GUI and rendering workers. */
static FcImage *load(const char *s) {
    lock();
    for (int i = 0; i < 8; ++i) if (cache[i] && !strcmp(cache[i]->base64, s)) {
        ++cache[i]->refs; FcImage *im = cache[i]; unlock(); return im;
    }
    unlock();
    size_t n; unsigned char *bytes = decode(s, &n);
    if (!bytes) return NULL;
    int w, h, comp;
    static const unsigned char png[] = {137,80,78,71,13,10,26,10};
    int format = (n >= 8 && !memcmp(bytes, png, 8)) ||
                 (n >= 3 && bytes[0] == 255 && bytes[1] == 216 && bytes[2] == 255);
    if (!format || !stbi_info_from_memory(bytes, (int)n, &w, &h, &comp) ||
        w <= 0 || h <= 0 || w > 8192 || h > 8192 || (size_t)w * h > 16u * 1024u * 1024u) {
        free(bytes); return NULL;
    }
    FcImage *im = calloc(1, sizeof *im);
    if (!im) { free(bytes); return NULL; }
    im->rgba = stbi_load_from_memory(bytes, (int)n, &im->width, &im->height, &comp, 4);
    free(bytes);
    if (!im->rgba) { free(im); return NULL; }
    im->base64 = malloc(strlen(s) + 1);
    if (!im->base64) { stbi_image_free(im->rgba); free(im); return NULL; }
    strcpy(im->base64, s);
    im->refs = 1; im->cost = strlen(s) + 1 + (size_t)w * h * 4;
    lock();
    for (int i = 0; i < 8; ++i) if (cache[i] && !strcmp(cache[i]->base64, s)) {
        FcImage *existing = cache[i]; ++existing->refs; release(im); unlock(); return existing;
    }
    const size_t limit = 64u * 1024u * 1024u;
    if (im->cost <= limit) {
        while (cache_bytes + im->cost > limit || cache[7]) {
            FcImage *old = cache[0];
            memmove(cache, cache + 1, 7 * sizeof *cache); cache[7] = NULL;
            if (old) { cache_bytes -= old->cost; release(old); }
        }
        int slot = 0; while (cache[slot]) ++slot;
        cache[slot] = im; ++im->refs; cache_bytes += im->cost;
    }
    unlock(); return im;
}
static int add(FcDrawing *d, FcImage *im, double x, double y, double w, double h,
               int rotation, int mirror, double opacity, int layer) {
    if (w <= 0 && h <= 0) w = 100;
    if (w <= 0) w = h * im->width / im->height;
    if (h <= 0) h = w * im->height / im->width;
    int index = -1;
    if (isfinite(x) && isfinite(y) && isfinite(w) && isfinite(h) &&
        x >= FC_MIN_COORD && y >= FC_MIN_COORD && x <= FC_MAX_COORD && y <= FC_MAX_COORD &&
        w >= .001 && h >= .001 && w <= FC_MAX_COORD && h <= FC_MAX_COORD &&
        isfinite(opacity) && opacity >= 0 && opacity <= 1 && layer >= 0 && layer < FC_MAX_LAYERS &&
        fc_image_hold(d, im)) {
        FcPrim *p = fc_drawing_push(d);
        if (p) {
            p->kind = FC_IMAGE; p->layer = layer;
            p->u.image.p = (FcPoint){x, y}; p->u.image.width = w; p->u.image.height = h;
            p->u.image.rotation = rotation % 360; p->u.image.mirror = !!mirror;
            p->u.image.opacity = opacity; p->u.image.bitmap = im;
            index = d->nprims - 1;
        }
    }
    lock(); release(im); unlock(); return index;
}
int fc_drawing_add_image(FcDrawing *d, const void *bytes, size_t n, double x, double y,
                         double w, double h, int layer) {
    if (!d || !bytes || !n || n > FC_IMAGE_MAX_BYTES || !isfinite(w) || !isfinite(h)) return -1;
    char *s = encode(bytes, n);
    if (!s) return -1;
    FcImage *im = load(s); free(s);
    return im ? add(d, im, x, y, w, h, 0, 0, 1, layer) : -1;
}
int fc_image_parse(FcDrawing *d, const char **tok, int n) {
    double x,y,w,h,a; int r,m,l;
    if (n != 11 || !fc_parse_double(tok[2], &x) || !fc_parse_double(tok[3], &y) ||
        !fc_parse_double(tok[4], &w) || !fc_parse_double(tok[5], &h) || w <= 0 || h <= 0 ||
        !fc_parse_int(tok[6], &r) || !fc_parse_int(tok[7], &m) || (m != 0 && m != 1) ||
        !fc_parse_double(tok[8], &a) || !fc_parse_int(tok[9], &l)) return 0;
    FcImage *im = load(tok[10]);
    return im && add(d, im, x,y,w,h,r,m,a,l) >= 0;
}
void fc_image_corners(const FcPrim *p, FcPoint out[4]) {
    double a = (p->u.image.rotation % 360) * 0.017453292519943295;
    double c = cos(a), s = sin(a), m = p->u.image.mirror ? -1 : 1;
    const int xs[] = {0,1,1,0}, ys[] = {0,0,1,1};
    for (int i = 0; i < 4; ++i) {
        double x = xs[i] * p->u.image.width * m, y = ys[i] * p->u.image.height;
        out[i] = (FcPoint){p->u.image.p.x + c*x - s*y, p->u.image.p.y + s*x + c*y};
    }
}
