#ifndef FIDOCAD_H
#define FIDOCAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FC_VERSION "0.1.0"

#define FC_MAX_LAYERS 16
#define FC_MAX_TOKENS 10000
#define FC_MAX_VERTICES 10000
#define FC_MAX_MACRO_DEPTH 16
#define FC_MIN_COORD 0.0
#define FC_MAX_COORD 1000000.0
#define FC_MAX_NUM 1e15 /* fc_fmt_num saturates here */
#define FC_MAX_EXPANDED_PRIMS 200000 /* per fc_expand, cached macro bodies included */

#define FC_STYLE_BOLD 1
#define FC_STYLE_ITALIC 2
#define FC_STYLE_MIRROR 4

#define FC_ARROW_START 1
#define FC_ARROW_END 2
#define FC_ARROW_LIMITER 1
#define FC_ARROW_EMPTY 2

#define FC_PAD_OVAL 0
#define FC_PAD_RECT 1
#define FC_PAD_ROUNDED 2

typedef enum {
    FC_OK = 0,
    FC_ERR_IO = 1,
    FC_ERR_NOMEM = 2,
    FC_ERR_ARG = 3
} FcStatus;

typedef enum {
    FC_DIAG_NOTE = 0,
    FC_DIAG_WARN = 1,
    FC_DIAG_ERR = 2
} FcDiagLevel;

typedef enum {
    FC_LINE = 0,
    FC_BEZIER,
    FC_RECT,
    FC_OVAL,
    FC_POLY,
    FC_CURVE,
    FC_CONN,
    FC_PCB_LINE,
    FC_PCB_PAD,
    FC_MACRO,
    FC_TEXT
} FcKind;

struct FcArenaBlock;
typedef struct FcArena {
    struct FcArenaBlock *blocks;
    struct FcArenaBlock *cur;
    size_t used;
} FcArena;

typedef struct {
    double x, y;
} FcPoint;

typedef struct {
    double min_x, min_y, max_x, max_y;
} FcRect;

typedef struct {
    int line;
    FcDiagLevel level;
    const char *msg;
} FcDiag;

typedef struct {
    FcPoint p;
    double size_y, size_x;
    int rotation, style, layer;
    const char *font;
    const char *text;
} FcLabel;

typedef struct {
    int arrows;
    int arrow_style;
    int dash;
    double arrow_len;
    double arrow_half_w;
} FcExt;

typedef struct {
    FcKind kind;
    int layer;
    int filled;
    int src_line;
    int has_ext;
    FcExt ext;
    int has_name, has_value;
    FcLabel name, value;
    int unresolved;
    union {
        struct {
            FcPoint a, b;
        } line;
        struct {
            FcPoint p[4];
        } bezier;
        struct {
            FcPoint a, b;
        } box;
        struct {
            FcPoint *pts;
            int n;
        } poly;
        struct {
            FcPoint *pts;
            int n;
            int closed;
        } curve;
        struct {
            FcPoint p;
        } conn;
        struct {
            FcPoint a, b;
            double width;
        } pcb_line;
        struct {
            FcPoint p;
            double rx, ry, hole;
            int style;
        } pad;
        struct {
            FcPoint p;
            int orient, mirror;
            const char *key;
        } macro;
        FcLabel text;
    } u;
} FcPrim;

typedef struct {
    char name[64];
    uint32_t rgb;
    float alpha;
    int modified;
} FcLayer;

typedef struct {
    double diameter_conn;
    double line_width;
    double line_width_circles;
    int has_img;
    double img_x, img_y, img_scale, img_alpha;
} FcConfig;

typedef struct FcDrawing {
    FcArena arena;
    FcPrim *prims;
    int nprims, cap_prims;
    FcLayer layers[FC_MAX_LAYERS];
    FcConfig cfg;
    FcDiag *diags;
    int ndiags, cap_diags;
} FcDrawing;

typedef struct FcMacro {
    const char *key;
    const char *long_name;
    const char *category;
    const char *lib_name;
    const char *prefix;
    const char *body;
    int nprims_hint;
} FcMacro;

typedef struct FcLib FcLib;

typedef struct {
    int nprims;
    int by_kind[11];
    int nmacros;
    int nunresolved;
    int nlabels;
    int layers_used;
    int ndiags;
    int nwarns;
    int nerrs;
    FcRect bbox;
    int has_bbox;
} FcStats;

typedef struct {
    int extensions; /* emit FCJ/FJC (default 1) */
    int header;     /* emit [FIDOCAD] (default 1) */
} FcEmitOpts;

typedef struct {
    int scale;      /* pixels per grid unit; 0 = auto (fits max_w x max_h) */
    int margin;     /* padding in pixels (0 allowed) */
    int max_w, max_h; /* <= 0 = default */
    int grid;
    uint32_t bg;    /* 0xRRGGBB, taken as-is (0 = black) */
    int ppm;        /* 1 = PPM, 0 = PNG */
    int tex;        /* 1 = typeset $...$ (default on) */
} FcRenderOpts;

typedef struct {
    int verbose; /* list every primitive */
} FcDescribeOpts;

/* Viewport for interactive rendering: pixel = logical * scale + origin. */
typedef struct {
    double ox, oy;     /* pixel position of logical (0,0) */
    double scale;      /* pixels per logical unit */
    uint32_t bg;       /* 0xRRGGBB */
    int grid;          /* grid step in logical units, 0 = none */
    int grid_major;    /* emphasise every Nth grid dot, 0 = none */
    uint32_t grid_rgb;
    int tex;
} FcView;

/* --- arena / IO --- */
void fc_arena_init(FcArena *a, size_t cap);
void fc_arena_free(FcArena *a);
void *fc_arena_alloc(FcArena *a, size_t n);
char *fc_arena_strndup(FcArena *a, const char *s, size_t n);
char *fc_arena_strdup(FcArena *a, const char *s);
int fc_read_file(const char *path, char **out, size_t *len);
int fc_write_file(const char *path, const char *data, size_t n);

/* --- drawing --- */
void fc_drawing_init(FcDrawing *d);
void fc_drawing_free(FcDrawing *d);
void fc_drawing_clear(FcDrawing *d);
FcPrim *fc_drawing_push(FcDrawing *d);
void fc_drawing_diag(FcDrawing *d, int line, FcDiagLevel level, const char *fmt, ...);
void fc_layers_init(FcLayer layers[FC_MAX_LAYERS]);
void fc_config_init(FcConfig *c);

/* len == 0 means text is NUL-terminated (strlen is used). */
int fc_parse(FcDrawing *d, const char *text, size_t len);
int fc_parse_file(FcDrawing *d, const char *path);

size_t fc_emit(const FcDrawing *d, char **out, const FcEmitOpts *opts);
int fc_emit_file(const FcDrawing *d, const char *path, const FcEmitOpts *opts);

/* --- libraries --- */
FcLib *fc_lib_new(void);
void fc_lib_free(FcLib *lib);
/* len == 0 means text is NUL-terminated (strlen is used). */
int fc_lib_load_text(FcLib *lib, const char *text, size_t len, const char *prefix);
int fc_lib_load_file(FcLib *lib, const char *path, const char *prefix);
int fc_lib_load_dir(FcLib *lib, const char *dir);
/* argv0 helps locate share/ when the OS cannot report the executable path */
void fc_set_argv0(const char *argv0);
int fc_lib_load_stdlib(FcLib *lib, const char *argv0);
/* Directory for the user's own macros: $FIDO_USER_LIB_DIR, else
   $XDG_DOCUMENTS_DIR/FidoLib, else ~/Documents/FidoLib. */
int fc_user_lib_dir(char *out, size_t cap);
int fc_lib_load_user_dir(FcLib *lib); /* silent no-op when the directory is absent */
const FcMacro *fc_lib_find(const FcLib *lib, const char *key);
int fc_lib_count(const FcLib *lib);
const FcMacro *fc_lib_at(const FcLib *lib, int i);
const char *fc_lib_prefix_for_file(const char *filename);

/* --- editing (GUI and agents) --- */
int fc_append_fcd(FcDrawing *d, const char *text, size_t len); /* returns prims added */
void fc_drawing_remove(FcDrawing *d, int index);
int fc_drawing_move(FcDrawing *d, int from, int to);
void fc_prim_translate(FcPrim *p, double dx, double dy);
void fc_prim_rotate(FcPrim *p, double cx, double cy); /* 90 deg clockwise about (cx,cy) */
void fc_prim_mirror(FcPrim *p, double cx);            /* horizontal flip about x = cx */
int fc_prim_bounds(const FcDrawing *d, const FcLib *lib, int index, FcRect *out);
int fc_hit_test(const FcDrawing *d, const FcLib *lib, double x, double y, double tol);
char *fc_prim_fcd(const FcPrim *p); /* one primitive as FCD text (malloc) */
/* which: 0 = TY text, 1 = name label, 2 = value label. Missing labels are
 * created next to the primitive; an empty string removes a name/value. */
int fc_prim_set_text(FcDrawing *d, int index, int which, const char *text);

/* Control points ("handles") of a primitive, like FidoCadJS virtualPoint[]:
 * endpoints for lines and PCB tracks, corners for rectangles/ellipses, the
 * vertices of polygons and curves, Bezier control points, and the position of
 * dots, pads, macros and text. fc_prim_handle returns 0 if i is out of range. */
int fc_prim_handles(const FcPrim *p);
int fc_prim_handle(const FcPrim *p, int i, FcPoint *out);
void fc_prim_set_handle(FcPrim *p, int i, double x, double y);

/* Diff two drawings primitive by primitive. json=0 gives a unified-style text
 * report ("- [i] ..." / "+ [j] ..."), json=1 an object with an ops array. */
char *fc_diff(const FcDrawing *before, const FcDrawing *after, int json);

/* --- expand / query --- */
int fc_expand(const FcDrawing *src, FcDrawing *dst, const FcLib *lib);
int fc_bounds(const FcDrawing *d, const FcLib *lib, FcRect *out);
void fc_stats(const FcDrawing *d, const FcLib *lib, FcStats *out);
char *fc_describe(const FcDrawing *d, const FcLib *lib, const FcDescribeOpts *opts);
char *fc_to_json(const FcDrawing *d, const FcLib *lib);
char *fc_context(const FcDrawing *d, const FcLib *lib);

const char *fc_kind_name(FcKind k);
const char *fc_kind_code(const FcPrim *p);
int fc_validate(const FcDrawing *d, char **report);

/* --- render --- */
void fc_render_opts_init(FcRenderOpts *o);
int fc_render_file(const FcDrawing *d, const FcLib *lib, const FcRenderOpts *opts,
                   const char *path);
int fc_render_rgb(const FcDrawing *d, const FcLib *lib, const FcView *v, uint8_t *rgb, int w,
                  int h, int stride);

/* --- numbers --- */
int fc_parse_int(const char *s, int *out);
int fc_parse_double(const char *s, double *out);
double fc_clamp_coord(double v);
int fc_clamp_layer(int l);
int fc_clamp_dash(int d);
void fc_fmt_num(char *buf, size_t cap, double v);

int fc_tex_has_math(const char *s);
int fc_tex_measure(const char *latex, int display, float *width_em, float *height_em,
                   float *depth_em);

#ifdef __cplusplus
}
#endif

#endif
