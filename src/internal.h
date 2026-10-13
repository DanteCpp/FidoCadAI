#ifndef FIDOCAD_INTERNAL_H
#define FIDOCAD_INTERNAL_H

#include "fidocad.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define FC_ALIGN 8
#define FC_GROW(cap) ((cap) < 8 ? 8 : (cap) * 2)
#define FC_PLACEHOLDER_SIZE 4.0 /* half-size of the box drawn for an unresolved macro */

typedef struct {
    char *p;
    size_t n, cap;
} FcBuf;

struct FcLib {
    FcArena arena;
    FcMacro *macros;
    int n, cap;
    int *slot; /* open-address hash of indices, -1 empty */
    int nslot;
};

void fc_buf_init(FcBuf *b);
void fc_buf_free(FcBuf *b);
int fc_buf_reserve(FcBuf *b, size_t extra);
void fc_buf_putc(FcBuf *b, char c);
void fc_buf_puts(FcBuf *b, const char *s);
void fc_buf_putn(FcBuf *b, const char *s, size_t n);
void fc_buf_printf(FcBuf *b, const char *fmt, ...);
char *fc_buf_steal(FcBuf *b);

int fc_streq(const char *a, const char *b);
int fc_parse_bool(const char *v); /* 0, false, no, off and "" are false */
size_t fc_strlen(const char *s);
void fc_tolower_buf(char *s);
void fc_prim_init(FcPrim *p);
void fc_label_default(FcLabel *l, double x, double y, int layer);
void fc_json_escape(FcBuf *b, const char *s);
void fc_emit_prim(FcBuf *b, const FcPrim *p, int extensions);
void fc_fmt_coord(char *buf, size_t cap, double v);

uint32_t fc_hash(const char *s);
const char *fc_layer_default_name(int i);
uint32_t fc_layer_default_rgb(int i);
float fc_layer_default_alpha(int i);

void fc_xform_point(FcPoint *pt, int orient, int mirror, double ox, double oy);
void fc_xform_label(FcLabel *l, int orient, int mirror, double ox, double oy);
void fc_xform_prim(FcPrim *p, int orient, int mirror, double ox, double oy);

int fc_code_kind(const char *tok, FcKind *kind, int *filled);

typedef struct {
    int math; /* 0 text, 1 inline, 2 display */
    const char *p;
    int n;
} FcTexSeg;

typedef struct {
    int kind; /* 0 glyph, 1 rule */
    float x, y, w, h, size;
    int glyph, italic, bold;
} FcTexOp;

typedef struct {
    FcTexOp *ops;
    int n, cap;
    float w, h, d;
} FcTexGeom;

int fc_tex_split(const char *text, FcTexSeg *out, int maxn);
int fc_tex_compile(const char *latex, int display, FcTexGeom *g);
void fc_tex_geom_free(FcTexGeom *g);
const unsigned char *fc_tex_glyph_bits(int gid);

int fc_utf8_next(const char **p, const char *end);
int fc_font_is_mono(const char *name);
int fc_font_family(const char *name); /* 0 sans, 1 serif, 2 mono */

/* The stroke-font drawing of a code point; unknown ones draw as '?'. Accented
 * Latin-1 letters are their base letter (an accented i or j without its dot)
 * plus an accent mark that the renderer adds. */
typedef struct {
    int left, right;  /* horizontal extent, font units */
    const int8_t *xy; /* n bytes of x,y pairs; a 127 pair lifts the pen */
    int n;
    int accent;       /* 0 none, 1 acute, 2 grave, 3 circumflex, 4 umlaut, 5 tilde, 6 cedilla */
    int lower;        /* lowercase base letter: the accent sits at x-height */
} FcStroke;
int fc_stroke_glyph(int cp, FcStroke *out);

typedef struct {
    int w, h, xoff, yoff;
    float advance; /* unrounded, so runs of glyphs scale smoothly */
    const uint8_t *cover;
} FcTtfGlyph;

/* sys.c: executable location and data-directory search */
void fc_dirname(const char *path, char *out, size_t cap);
int fc_exe_path(char *out, size_t cap);
/* Candidate directories for share/<sub>, in search order: next to the binary
 * (an installed prefix or the build tree), the usual install prefixes, then
 * ./share. Returns how many were written. */
#define FC_DATA_DIRS 7
#define FC_DIR_MAX 4160
int fc_data_dirs(const char *sub, char dirs[FC_DATA_DIRS][FC_DIR_MAX]);
/* User-owned application data: $FCAI_HOME, else ~/.fcai. */
int fc_app_dir(char *out, size_t cap);

/* CLI-only style profile support (style.c). */
int fc_cmd_style(int argc, char **argv);
int fc_style_context(FcBuf *b);

int fc_ttf_init(void);
int fc_ttf_ready(void);
void fc_ttf_shutdown(void);
int fc_ttf_resolve(int family, int bold, int italic, int *fake_italic);
float fc_ttf_ascent(int slot, float px_size);
/* shift_x/shift_y: sub-pixel origin in [0,1), quantized to FC_TTF_SUBPIX steps */
#define FC_TTF_SUBPIX 4
float fc_ttf_quantize(float px_size); /* the size glyphs are actually cached at */
int fc_ttf_glyph(int slot, float px_size, float xratio, int cp, float shift_x, float shift_y,
                 FcTtfGlyph *out);

#endif
