#ifndef FIDOCAD_H
#define FIDOCAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
int fc_lib_load_stdlib(FcLib *lib, const char *argv0);
const FcMacro *fc_lib_find(const FcLib *lib, const char *key);
int fc_lib_count(const FcLib *lib);
const FcMacro *fc_lib_at(const FcLib *lib, int i);
const char *fc_lib_prefix_for_file(const char *filename);

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

#endif
