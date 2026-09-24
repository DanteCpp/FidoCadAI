#include "internal.h"
#include "stb_truetype.h"

#include <math.h>
#include <stdio.h>
#include <sys/stat.h>

typedef struct {
    unsigned char *data;
    stbtt_fontinfo info;
    int loaded;
} Face;

enum { FAM_SANS = 0, FAM_SERIF = 1, FAM_MONO = 2, N_FACE = 12 };

static Face faces[N_FACE];
static int ttf_inited;

static const char *k_face_file[N_FACE] = {
    "LiberationSans-Regular.ttf",    "LiberationSans-Bold.ttf",
    "LiberationSans-Italic.ttf",     "LiberationSans-BoldItalic.ttf",
    "LiberationSerif-Regular.ttf",   "LiberationSerif-Bold.ttf",
    "LiberationSerif-Italic.ttf",    "LiberationSerif-BoldItalic.ttf",
    "LiberationMono-Regular.ttf",    "LiberationMono-Bold.ttf",
    "LiberationMono-Italic.ttf",     "LiberationMono-BoldItalic.ttf",
};

typedef struct {
    uint64_t key;
    int w, h, xoff, yoff, advance;
    uint8_t *pix;
} CacheEnt;

#define CACHE_N 2048
static CacheEnt cache[CACHE_N];
static int cache_count;

static int load_face(Face *f, const char *path) {
    char *buf = NULL;
    size_t n = 0;
    int off;
    if (fc_read_file(path, &buf, &n) != FC_OK || !buf || n < 100) {
        free(buf);
        return 0;
    }
    off = stbtt_GetFontOffsetForIndex((unsigned char *)buf, 0);
    if (off < 0 || !stbtt_InitFont(&f->info, (unsigned char *)buf, off)) {
        free(buf);
        return 0;
    }
    f->data = (unsigned char *)buf;
    f->loaded = 1;
    return 1;
}

static int try_dir(const char *dir) {
    char path[1400];
    int i, n = 0;
    struct stat st;
    if (!dir || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    for (i = 0; i < N_FACE; i++) {
        if (faces[i].loaded) continue;
        snprintf(path, sizeof path, "%s/%s", dir, k_face_file[i]);
        if (load_face(&faces[i], path)) n++;
    }
    return n;
}

/* keep searching: faces missing in one directory may be in the next */
static int try_dir_cb(void *ctx, const char *dir) {
    (void)ctx;
    try_dir(dir);
    return 0;
}

int fc_ttf_init(void) {
    const char *env;
    int i;
    if (ttf_inited) return fc_ttf_ready();
    ttf_inited = 1;
    env = getenv("FIDO_FONT_DIR");
    if (env) try_dir(env);
    fc_find_data("fonts", try_dir_cb, NULL);
    /* system copies of Liberation (Linux packages, macOS font folders) */
    try_dir("/usr/share/fonts/liberation");
    try_dir("/usr/share/fonts/truetype/liberation");
    try_dir("/usr/share/fonts/TTF");
    env = getenv("HOME");
    if (env) {
        char cand[1400];
        snprintf(cand, sizeof cand, "%s/Library/Fonts", env);
        try_dir(cand);
    }
    try_dir("/Library/Fonts");
    /* Regular is required for a family to count as available */
    for (i = 0; i < 3; i++) {
        if (!faces[i * 4].loaded) {
            /* promote any loaded style of that family to Regular */
            int s;
            for (s = 1; s < 4; s++) {
                if (faces[i * 4 + s].loaded) {
                    faces[i * 4] = faces[i * 4 + s];
                    break;
                }
            }
        }
    }
    return fc_ttf_ready();
}

int fc_ttf_ready(void) { return faces[0].loaded || faces[4].loaded || faces[8].loaded; }

void fc_ttf_shutdown(void) {
    int i;
    for (i = 0; i < N_FACE; i++) {
        if (faces[i].loaded && faces[i].data) {
            /* Regular may alias another style; skip double-free by zeroing data */
            int j;
            unsigned char *d = faces[i].data;
            free(d);
            for (j = 0; j < N_FACE; j++)
                if (faces[j].data == d) {
                    faces[j].data = NULL;
                    faces[j].loaded = 0;
                }
        }
    }
    for (i = 0; i < CACHE_N; i++) {
        free(cache[i].pix);
        cache[i].pix = NULL;
        cache[i].key = 0;
    }
    cache_count = 0;
    ttf_inited = 0;
}

int fc_ttf_resolve(int family, int bold, int italic, int *fake_italic) {
    int base, style, slot;
    if (family < 0 || family > 2) family = 0;
    if (!ttf_inited) fc_ttf_init();
    *fake_italic = 0;
    style = (bold ? 1 : 0) | (italic ? 2 : 0);
    base = family * 4;
    slot = base + style;
    if (faces[slot].loaded) return slot;
    if (italic && bold && faces[base + 1].loaded) {
        *fake_italic = 1;
        return base + 1;
    }
    if (italic && faces[base + 2].loaded) return base + 2;
    if (bold && faces[base + 1].loaded) {
        if (italic) *fake_italic = 1;
        return base + 1;
    }
    if (faces[base].loaded) {
        if (italic) *fake_italic = 1;
        return base;
    }
    /* fall across families: sans */
    if (family != 0 && faces[0].loaded) {
        if (italic) *fake_italic = 1;
        return 0;
    }
    return -1;
}

float fc_ttf_ascent(int slot, float px_size) {
    int asc, desc, gap;
    float sc;
    if (slot < 0 || slot >= N_FACE || !faces[slot].loaded) return px_size * 0.8f;
    sc = stbtt_ScaleForPixelHeight(&faces[slot].info, px_size);
    stbtt_GetFontVMetrics(&faces[slot].info, &asc, &desc, &gap);
    return (float)asc * sc;
}

static uint64_t cache_key(int slot, int cp, float px, float xratio) {
    uint32_t ph = (uint32_t)(px * 8.0f + 0.5f);
    uint32_t xr = (uint32_t)(xratio * 256.0f + 0.5f);
    return ((uint64_t)(uint32_t)cp << 32) | ((uint64_t)slot << 24) | ((uint64_t)(ph & 0xFFF) << 12) |
           (xr & 0xFFF);
}

static CacheEnt *cache_get(uint64_t key) {
    uint32_t h = (uint32_t)(key ^ (key >> 33)) * 0x9e3779b1u;
    int i = (int)(h & (CACHE_N - 1));
    int n = CACHE_N;
    while (n--) {
        if (!cache[i].key) return NULL;
        if (cache[i].key == key) return &cache[i];
        i = (i + 1) & (CACHE_N - 1);
    }
    return NULL;
}

static CacheEnt *cache_put(uint64_t key) {
    uint32_t h = (uint32_t)(key ^ (key >> 33)) * 0x9e3779b1u;
    int i = (int)(h & (CACHE_N - 1));
    int n = CACHE_N;
    int hole = -1;
    while (n--) {
        if (!cache[i].key) {
            hole = i;
            break;
        }
        if (cache[i].key == key) return &cache[i];
        i = (i + 1) & (CACHE_N - 1);
    }
    if (hole < 0) {
        /* table full: drop this slot */
        i = (int)(h & (CACHE_N - 1));
        free(cache[i].pix);
        cache[i].pix = NULL;
        hole = i;
    }
    cache[hole].key = key;
    cache[hole].pix = NULL;
    cache_count++;
    return &cache[hole];
}

int fc_ttf_glyph(int slot, float px_size, float xratio, int cp, FcTtfGlyph *out) {
    uint64_t key;
    CacheEnt *e;
    Face *f;
    float sx, sy;
    int x0, y0, x1, y1, w, h, adv, lsb;
    uint8_t *pix;
    memset(out, 0, sizeof *out);
    if (slot < 0 || slot >= N_FACE || !faces[slot].loaded || px_size < 2) return 0;
    if (cp <= 0) return 0;
    f = &faces[slot];
    sy = stbtt_ScaleForPixelHeight(&f->info, px_size);
    sx = sy * (xratio > 0.05f ? xratio : 1.0f);
    key = cache_key(slot, cp, px_size, xratio);
    e = cache_get(key);
    if (e) {
        out->w = e->w;
        out->h = e->h;
        out->xoff = e->xoff;
        out->yoff = e->yoff;
        out->advance = e->advance;
        out->cover = e->pix;
        return 1;
    }
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    stbtt_GetCodepointBitmapBox(&f->info, cp, sx, sy, &x0, &y0, &x1, &y1);
    w = x1 - x0;
    h = y1 - y0;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w > 512) w = 512;
    if (h > 512) h = 512;
    pix = NULL;
    if (w > 0 && h > 0) {
        pix = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
        if (!pix) return 0;
        stbtt_MakeCodepointBitmap(&f->info, pix, w, h, w, sx, sy, cp);
    }
    e = cache_put(key);
    e->w = w;
    e->h = h;
    e->xoff = x0;
    e->yoff = y0;
    e->advance = (int)floor(adv * sx + 0.5);
    if (e->advance < 1 && cp != ' ') e->advance = w + 1;
    e->pix = pix;
    out->w = w;
    out->h = h;
    out->xoff = x0;
    out->yoff = y0;
    out->advance = e->advance;
    out->cover = pix;
    return 1;
}
