#include "internal.h"
#include "mimetex/embedded.h"
#include <ctype.h>
#include <stdatomic.h>

/* Cache layout at a fixed high resolution, independent of zoom, colour and
 * rotation. Acquired images remain valid when another call evicts the entry. */
typedef struct TexImage {
    struct TexImage *next;
    char *source;
    int display, refs, ok;
    FcTexGeom geom;
    size_t bytes;
} TexImage;
static TexImage *images;
static size_t cache_bytes;
static int cache_count;
static atomic_flag tex_lock = ATOMIC_FLAG_INIT;
#define TEX_CACHE_BYTES (16u * 1024u * 1024u)
#define TEX_CACHE_COUNT 64
#define TEX_SOURCE_MAX 4096

static void lock_tex(void) { while (atomic_flag_test_and_set(&tex_lock)) {} }
static void unlock_tex(void) { atomic_flag_clear(&tex_lock); }
static void release(TexImage *im) {
    if (--im->refs) return;
    free((void *)im->geom.pixels);
    free((void *)im->geom.sum);
    free(im->source);
    free(im);
}

/* Ignore escaped dollars and consume backslash pairs as literal characters. */
static const char *dollar(const char *p, int display) {
    for (; *p; ++p) {
        if (*p == '\\' && p[1]) { ++p; continue; }
        if (*p == '$' && (!display || p[1] == '$')) return p;
    }
    return NULL;
}
int fc_tex_has_math(const char *s) { return s && dollar(s, 0) != NULL; }

int fc_tex_split(const char *text, FcTexSeg *out, int maxn) {
    const char *p = text;
    int n = 0;
    if (!p) return 0;
    while (*p && n < maxn) {
        const char *start = dollar(p, 0), *end;
        int display, delim;
        if (!start) { out[n++] = (FcTexSeg){0, p, (int)strlen(p)}; break; }
        if (start > p) {
            out[n++] = (FcTexSeg){0, p, (int)(start - p)};
            if (n == maxn) break;
        }
        display = start[1] == '$';
        delim = display ? 2 : 1;
        end = dollar(start + delim, display);
        if (!end) {
            out[n++] = (FcTexSeg){0, start, (int)strlen(start)};
            break;
        }
        out[n++] = (FcTexSeg){display ? 2 : 1, start + delim, (int)(end - start - delim)};
        p = end + delim;
    }
    return n;
}

/* mimeTeX predates widespread Unicode input. Normalize common mathematical
 * code points without changing the document or the editable source. */
static const struct { int cp; const char *tex; } unicode_math[] = {
    {0xB0,"^{\\circ}"},{0xB1,"\\pm "},{0xB2,"^{2}"},{0xB3,"^{3}"},{0xB5,"\\mu "},
    {0xD7,"\\times "},{0xF7,"\\div "},
    {0x391,"A"},{0x392,"B"},{0x393,"\\Gamma "},{0x394,"\\Delta "},{0x395,"E"},
    {0x396,"Z"},{0x397,"H"},{0x398,"\\Theta "},{0x399,"I"},{0x39A,"K"},
    {0x39B,"\\Lambda "},{0x39C,"M"},{0x39D,"N"},{0x39E,"\\Xi "},{0x39F,"O"},
    {0x3A0,"\\Pi "},{0x3A1,"P"},{0x3A3,"\\Sigma "},{0x3A4,"T"},
    {0x3A5,"\\Upsilon "},{0x3A6,"\\Phi "},{0x3A7,"X"},{0x3A8,"\\Psi "},{0x3A9,"\\Omega "},
    {0x3B1,"\\alpha "},{0x3B2,"\\beta "},{0x3B3,"\\gamma "},{0x3B4,"\\delta "},
    {0x3B5,"\\epsilon "},{0x3B6,"\\zeta "},{0x3B7,"\\eta "},{0x3B8,"\\theta "},
    {0x3B9,"\\iota "},{0x3BA,"\\kappa "},{0x3BB,"\\lambda "},{0x3BC,"\\mu "},
    {0x3BD,"\\nu "},{0x3BE,"\\xi "},{0x3BF,"o"},{0x3C0,"\\pi "},
    {0x3C1,"\\rho "},{0x3C2,"\\varsigma "},{0x3C3,"\\sigma "},{0x3C4,"\\tau "},
    {0x3C5,"\\upsilon "},{0x3C6,"\\phi "},{0x3C7,"\\chi "},{0x3C8,"\\psi "},{0x3C9,"\\omega "},
    {0x3D1,"\\vartheta "},{0x3D5,"\\varphi "},{0x3D6,"\\varpi "},
    {0x2032,"'"},{0x2033,"''"},{0x2070,"^{0}"},{0x2071,"^{i}"},
    {0x2074,"^{4}"},{0x2075,"^{5}"},{0x2076,"^{6}"},{0x2077,"^{7}"},
    {0x2078,"^{8}"},{0x2079,"^{9}"},
    {0x2080,"_{0}"},{0x2081,"_{1}"},{0x2082,"_{2}"},{0x2083,"_{3}"},
    {0x2084,"_{4}"},{0x2085,"_{5}"},{0x2086,"_{6}"},{0x2087,"_{7}"},
    {0x2088,"_{8}"},{0x2089,"_{9}"},
    {0x210F,"\\hbar "},{0x2202,"\\partial "},{0x2207,"\\nabla "},
    {0x220F,"\\prod "},{0x2211,"\\sum "},{0x2212,"-"},{0x221A,"\\sqrt "},
    {0x221E,"\\infty "},{0x222B,"\\int "},{0x2248,"\\approx "},
    {0x2260,"\\ne "},{0x2261,"\\equiv "},{0x2264,"\\leq "},{0x2265,"\\geq "},
    {0x22C5,"\\cdot "},{0x2190,"\\leftarrow "},{0x2192,"\\rightarrow "}
};

static char *prepare(const char *s) {
    FcBuf b;
    const char *p = s, *end = s + strlen(s);
    int depth = 0;
    fc_buf_init(&b);
    while (p < end) {
        int cp;
        if (*p == '\\' && p + 1 < end) {
            /* Backslash and the next character form a token. In particular,
             * escaped braces/dollars do not affect group validation. */
            fc_buf_putc(&b, *p++);
            fc_buf_putc(&b, *p++);
            continue;
        }
        if (*p == '{' && ++depth > 48) goto fail;
        if (*p == '}' && --depth < 0) goto fail;
        cp = fc_utf8_next(&p, end);
        if (cp < 128) fc_buf_putc(&b, (char)cp);
        else {
            size_t i;
            for (i = 0; i < sizeof unicode_math / sizeof *unicode_math; ++i)
                if (unicode_math[i].cp == cp) break;
            if (i == sizeof unicode_math / sizeof *unicode_math) goto fail;
            fc_buf_puts(&b, unicode_math[i].tex);
        }
        if (b.n > TEX_SOURCE_MAX) goto fail;
    }
    if (depth) goto fail;
    return fc_buf_steal(&b);
fail:
    fc_buf_free(&b);
    return NULL;
}

static TexImage *compile(const char *source, int display) {
    TexImage *im = calloc(1, sizeof *im);
    char *normalized;
    uint8_t *pixels = NULL;
    uint32_t *sum;
    size_t n = strlen(source);
    int x, y;
    if (!im) return NULL;
    im->refs = 1; /* cache reference */
    im->display = !!display;
    im->source = malloc(n + 1);
    if (!im->source) { release(im); return NULL; }
    memcpy(im->source, source, n + 1);
    im->bytes = sizeof *im + n + 1;
    normalized = prepare(source);
    if (normalized) {
        /* mimeprep expands its buffer in place. Reserve its documented maximum. */
        char *buffer = calloc(32768, 1);
        if (buffer) {
            memcpy(buffer, normalized, strlen(normalized) + 1);
            im->ok = fc_mimetex_render(buffer, display, &pixels, &im->geom.width,
                                      &im->geom.height, &im->geom.baseline, &im->geom.em);
        }
        free(buffer);
        free(normalized);
    }
    if (!im->ok) return im; /* cache failures too */
    im->geom.pixels = pixels;
    n = (size_t)(im->geom.width + 1) * (im->geom.height + 1);
    sum = calloc(n, sizeof *sum);
    if (!sum) { im->ok = 0; free(pixels); im->geom.pixels = NULL; return im; }
    im->geom.sum = sum;
    for (y = 0; y < im->geom.height; ++y) {
        uint32_t row = 0;
        for (x = 0; x < im->geom.width; ++x) {
            row += pixels[y * im->geom.width + x];
            sum[(y + 1) * (im->geom.width + 1) + x + 1] =
                sum[y * (im->geom.width + 1) + x + 1] + row;
        }
    }
    im->bytes += (size_t)im->geom.width * im->geom.height + n * sizeof *sum;
    im->geom.w = im->geom.width / im->geom.em;
    im->geom.h = im->geom.baseline / im->geom.em;
    im->geom.d = (im->geom.height - im->geom.baseline) / im->geom.em;
    im->geom.owner = im;
    return im;
}

int fc_tex_compile(const char *source, int display, FcTexGeom *g) {
    TexImage **link, *im;
    memset(g, 0, sizeof *g);
    if (!source || !*source || strlen(source) > TEX_SOURCE_MAX) return 0;
    lock_tex();
    for (link = &images; (im = *link); link = &im->next)
        if (im->display == !!display && strcmp(im->source, source) == 0) break;
    if (im) *link = im->next; /* most recently used first */
    else {
        im = compile(source, display);
        if (!im) { unlock_tex(); return 0; }
        ++cache_count;
        cache_bytes += im->bytes;
    }
    im->next = images;
    images = im;
    if (im->ok) { ++im->refs; *g = im->geom; }
    while (cache_count > TEX_CACHE_COUNT || cache_bytes > TEX_CACHE_BYTES) {
        TexImage **tail = &images, *old;
        while ((*tail)->next) tail = &(*tail)->next;
        old = *tail;
        *tail = NULL;
        cache_bytes -= old->bytes;
        --cache_count;
        release(old);
    }
    unlock_tex();
    return g->owner != NULL;
}

void fc_tex_geom_free(FcTexGeom *g) {
    if (g->owner) {
        lock_tex();
        release(g->owner);
        unlock_tex();
    }
    memset(g, 0, sizeof *g);
}

int fc_tex_measure(const char *source, int display, float *w, float *h, float *d) {
    FcTexGeom g;
    int ok = fc_tex_compile(source, display, &g);
    if (w) *w = g.w;
    if (h) *h = g.h;
    if (d) *d = g.d;
    fc_tex_geom_free(&g);
    return ok;
}
