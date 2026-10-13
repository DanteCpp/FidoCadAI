#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *k_help =
    "fcai " FC_VERSION " — FidoCad interpreter (C, AI-centric)\n"
    "\n"
    "Usage:\n"
    "  fcai [global-opts] <command> [command-opts] [file]\n"
    "\n"
    "file may be omitted or \"-\" for stdin.\n"
    "\n"
    "Commands:\n"
    "  context    Agent summary: stats, macros, layers, diags + JSON (default)\n"
    "  describe   Human-readable drawing summary\n"
    "  json       Machine-readable dump of the drawing\n"
    "  emit       Write canonical FCD\n"
    "  validate   Parse, re-emit, re-parse; report round-trip + diagnostics\n"
    "  render     Rasterize to PNG (or PPM with --ppm)\n"
    "  expand     Expand macros to primitives and emit FCD\n"
    "  lib        List macros in loaded libraries\n"
    "  macro KEY  Expanded JSON geometry at anchor (100,100), or --at X Y\n"
    "  lint       Advisory wire/dot geometry checks (--json supported)\n"
    "  hit FILE --at X Y [--tol N]   Pick a primitive index\n"
    "  find FILE --bbox X0 Y0 X1 Y1  Indices intersecting bounds\n"
    "  edit move|rm|add|set|layer FILE  Structured edit; emit result to stdout/-o\n"
    "  config     Show effective settings\n"
    "  diff A B   Primitive-level changes from A to B (--json for ops)\n"
    "  gui        Open the drawing editor (fcai-gui) for the user; edits reach\n"
    "             the agent live, Ctrl+Return ends the session\n"
    "  session    Talk to a running GUI (`session --help` lists commands)\n"
    "  skill      Print the fidocad agent skill; install, uninstall or locate it\n"
    "             (`skill --help` lists commands)\n"
    "  style      Persistent drawing preferences (`style --help` lists commands)\n"
    "  help       This help\n"
    "  version    Print version\n"
    "\n"
    "Global options (all AI-configurable):\n"
    "  --lib PATH           Load a .fcl library (repeatable)\n"
    "  --lib-dir PATH       Load every *.fcl in a directory (repeatable)\n"
    "  --stdlib             Load bundled standard libraries (default on)\n"
    "  --no-stdlib          Do not load the standard libraries\n"
    "  --prefix NAME        Prefix for the next --lib\n"
    "  --extensions         Emit FCJ/FJC (default on)\n"
    "  --no-extensions      Legacy FidoCAD output (no FCJ/FJC)\n"
    "  --config PATH        Load key=value settings file\n"
    "  -o, --out PATH       Output file (default stdout)\n"
    "  --json               Force JSON wrapping where applicable\n"
    "  -q, --quiet          Errors only\n"
    "  -v, --verbose        List every primitive in describe/context\n"
    "\n"
    "Render options:\n"
    "  --crop X0 Y0 X1 Y1   Render a region in logical drawing coordinates\n"
    "  --scale N            Pixels per grid unit (default: auto)\n"
    "  --margin N           Padding in pixels (default 12)\n"
    "  --width N --height N Max image size for auto scale\n"
    "  --grid               Draw a light grid\n"
    "  --ppm                Write PPM instead of PNG\n"
    "  --bg RRGGBB          Background colour (default FFFFFF)\n"
    "  --tex / --no-tex     Typeset $...$ LaTeX in TY text (default on)\n"
    "\n"
    "Query/edit options:\n"
    "  macro: --orient 0..3 --mirror (optional)\n"
    "  find: --near X Y R (bounding square) or --bbox X0 Y0 X1 Y1\n"
    "  edit move: --index N --by DX DY (carries attached labels)\n"
    "  edit rm: --index N; edit add: --from FILE\n"
    "  edit set: --index N --layer 0..15 --dash 0..4 --name TEXT --value TEXT\n"
    "            --stroke W (line width, FCJ; 0 = drawing default FJC B/C)\n"
    "            --text TEXT (standalone TY); supply only fields to change\n"
    "  edit layer: --layer 0..15 --color RRGGBB --alpha 0..1 --name TEXT\n"
    "            (FJC L/N: colour, opacity, name) or --reset to the default\n"
    "\n"
    "Environment (booleans: 0/false/no/off = off, anything else = on):\n"
    "  FIDO_STDLIB_DIR      Directory with FCDstdlib_en.fcl etc.\n"
    "  FIDO_LIB_DIR         Extra .fcl directory (loaded after stdlib)\n"
    "  FCAI_HOME           User data directory (default ~/.fcai): style.md + lib/\n"
    "  FIDO_USER_LIB_DIR    Your macro directory (default ~/.fcai/lib)\n"
    "  FIDO_FONT_DIR        Directory with the Liberation TTFs\n"
    "  FIDO_NO_STDLIB=1     Same as --no-stdlib\n"
    "  FIDO_NO_TEX=1        Same as --no-tex\n"
    "  FIDO_SCALE, FIDO_MARGIN, FIDO_JSON=1\n"
    "\n"
    "Config file (key = value, # comments):\n"
    "  stdlib, lib_dir, extensions, scale, margin, width, height, grid, ppm, bg, verbose, tex,\n"
    "  json, out\n";

typedef struct {
    char cmd[32];
    const char *file;
    const char *file2;
    const char *out;
    const char *config_path;
    int stdlib;
    int extensions;
    int json;
    int quiet;
    int verbose;
    int scale, margin, max_w, max_h, grid, ppm, tex;
    uint32_t bg;
    char prefix[128];
    const char *libs[64], *lib_prefixes[64]; /* --lib, with the --prefix given before it */
    int nlibs;
    const char *libdirs[16];
    int nlibdirs;
    int has_crop, has_at, has_box, index, layer, dash, orient, mirror;
    FcRect crop, box;
    double x, y, tolerance, dx, dy, stroke, alpha;
    const char *action, *from, *name, *value, *text;
    int has_color, reset;
    uint32_t color;
} Opt;

static void opt_init(Opt *o) {
    memset(o, 0, sizeof *o);
    o->index = o->layer = o->dash = -1;
    o->tolerance = 2;
    o->stroke = -1;
    o->alpha = -1;
    o->stdlib = 1;
    o->extensions = 1;
    o->margin = 12;
    o->max_w = 1200;
    o->max_h = 900;
    o->bg = 0xFFFFFF;
    o->tex = 1;
    if (fc_env_flag("FIDO_NO_STDLIB", 0)) o->stdlib = 0;
    if (fc_env_flag("FIDO_NO_TEX", 0)) o->tex = 0;
    if (fc_env_flag("FIDO_JSON", 0)) o->json = 1;
    {
        const char *s = getenv("FIDO_SCALE");
        int v;
        if (s && fc_parse_int(s, &v)) o->scale = v;
        s = getenv("FIDO_MARGIN");
        if (s && fc_parse_int(s, &v)) o->margin = v;
    }
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + fc_strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int parse_bg(const char *s, uint32_t *out) {
    unsigned v = 0;
    if (s[0] == '#') s++;
    if (sscanf(s, "%x", &v) != 1) return 0;
    *out = v & 0xFFFFFFu;
    return 1;
}

static void apply_kv(Opt *o, const char *k, const char *v) {
    int n;
    if (fc_streq(k, "stdlib")) o->stdlib = fc_parse_bool(v);
    else if (fc_streq(k, "extensions"))
        o->extensions = fc_parse_bool(v);
    else if (fc_streq(k, "verbose"))
        o->verbose = fc_parse_bool(v);
    else if (fc_streq(k, "json"))
        o->json = fc_parse_bool(v);
    else if (fc_streq(k, "grid"))
        o->grid = fc_parse_bool(v);
    else if (fc_streq(k, "ppm"))
        o->ppm = fc_parse_bool(v);
    else if (fc_streq(k, "tex"))
        o->tex = fc_parse_bool(v);
    else if (fc_streq(k, "scale") && fc_parse_int(v, &n))
        o->scale = n;
    else if (fc_streq(k, "margin") && fc_parse_int(v, &n))
        o->margin = n;
    else if (fc_streq(k, "width") && fc_parse_int(v, &n))
        o->max_w = n;
    else if (fc_streq(k, "height") && fc_parse_int(v, &n))
        o->max_h = n;
    else if (fc_streq(k, "bg"))
        parse_bg(v, &o->bg);
    else if (fc_streq(k, "lib_dir") && o->nlibdirs < 16)
        o->libdirs[o->nlibdirs++] = strdup(v);
    else if (fc_streq(k, "out"))
        o->out = strdup(v);
}

static int load_config(Opt *o, const char *path) {
    char *text = NULL;
    size_t len = 0;
    char *p, *line;
    if (fc_read_file(path, &text, &len) != FC_OK) return FC_ERR_IO;
    p = text;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        char *eq;
        if (nl) *nl = 0;
        line = trim(p);
        if (line[0] && line[0] != '#') {
            eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                apply_kv(o, trim(line), trim(eq + 1));
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    free(text);
    return FC_OK;
}

static void add_lib(Opt *o, const char *path) {
    if (o->nlibs >= 64) return;
    o->libs[o->nlibs] = path;
    o->lib_prefixes[o->nlibs] = o->prefix[0] ? strdup(o->prefix) : NULL;
    o->nlibs++;
    o->prefix[0] = 0;
}

static void lib_report(void *ctx, int error, const char *msg) {
    if (error || !((const Opt *)ctx)->quiet)
        fprintf(stderr, "fcai: %s%s\n", error ? "" : "warning: ", msg);
}

/* Flags macros missing from the libraries, and copies the expander's own warnings
 * (recursive or too deeply nested macros, size cap) into the drawing's diagnostics. */
static void check_macros(FcDrawing *d, const FcLib *lib) {
    int i, nmacros = 0;
    for (i = 0; i < d->nprims; i++) {
        FcPrim *p = &d->prims[i];
        if (p->kind != FC_MACRO) continue;
        nmacros++;
        if (!fc_lib_find(lib, p->u.macro.key)) {
            p->unresolved = 1;
            fc_drawing_diag(d, p->src_line, FC_DIAG_WARN, "unresolved macro '%s'",
                            p->u.macro.key ? p->u.macro.key : "");
        }
    }
    if (nmacros) {
        FcDrawing exp;
        fc_drawing_init(&exp);
        fc_expand(d, &exp, lib);
        for (i = 0; i < exp.ndiags; i++)
            fc_drawing_diag(d, exp.diags[i].line, exp.diags[i].level, "%s",
                            exp.diags[i].msg ? exp.diags[i].msg : "");
        fc_drawing_free(&exp);
    }
}

static int write_out(const Opt *o, const char *data, size_t n) {
    return fc_write_file(o->out ? o->out : "-", data, n);
}

static int cmd_lib(const Opt *o, FcLib *lib) {
    FcBuf b;
    int i, n = fc_lib_count(lib);
    fc_buf_init(&b);
    if (o->json) {
        fc_buf_printf(&b, "{\"count\":%d,\"macros\":[", n);
        for (i = 0; i < n; i++) {
            const FcMacro *m = fc_lib_at(lib, i);
            if (i) fc_buf_putc(&b, ',');
            fc_buf_puts(&b, "{\"key\":");
            fc_json_escape(&b, m->key);
            fc_buf_puts(&b, ",\"name\":");
            fc_json_escape(&b, m->long_name ? m->long_name : "");
            fc_buf_puts(&b, ",\"category\":");
            fc_json_escape(&b, m->category ? m->category : "");
            fc_buf_puts(&b, ",\"library\":");
            fc_json_escape(&b, m->lib_name ? m->lib_name : "");
            fc_buf_putc(&b, '}');
        }
        fc_buf_puts(&b, "]}\n");
    } else {
        fc_buf_printf(&b, "%d macros\n", n);
        for (i = 0; i < n; i++) {
            const FcMacro *m = fc_lib_at(lib, i);
            fc_buf_printf(&b, "%s\t%s\t%s\n", m->key, m->category ? m->category : "",
                          m->long_name ? m->long_name : "");
        }
    }
    {
        int rc = write_out(o, b.p ? b.p : "", b.n);
        fc_buf_free(&b);
        return rc;
    }
}

static const char *const k_env[] = {"FIDO_STDLIB_DIR", "FIDO_LIB_DIR", "FIDO_FONT_DIR",
                                    "FIDO_NO_STDLIB",  "FIDO_NO_TEX",  "FIDO_SCALE",
                                    "FIDO_MARGIN",     "FIDO_JSON",    "FCAI_HOME",
                                    "FIDO_USER_LIB_DIR", NULL};

static int cmd_config(const Opt *o, const FcLib *lib) {
    FcBuf b;
    int i;
    fc_buf_init(&b);
    fc_buf_printf(&b, "version = %s\n", FC_VERSION);
    fc_buf_printf(&b, "stdlib = %d\n", o->stdlib);
    {
        char ulib[1024];
        if (fc_user_lib_dir(ulib, sizeof ulib)) fc_buf_printf(&b, "user_lib = %s\n", ulib);
        if (fc_app_dir(ulib, sizeof ulib)) fc_buf_printf(&b, "style_profile = %s/style.md\n", ulib);
    }
    fc_buf_printf(&b, "extensions = %d\n", o->extensions);
    fc_buf_printf(&b, "json = %d\n", o->json);
    fc_buf_printf(&b, "verbose = %d\n", o->verbose);
    fc_buf_printf(&b, "scale = %d\n", o->scale);
    fc_buf_printf(&b, "margin = %d\n", o->margin);
    fc_buf_printf(&b, "width = %d\n", o->max_w);
    fc_buf_printf(&b, "height = %d\n", o->max_h);
    fc_buf_printf(&b, "grid = %d\n", o->grid);
    fc_buf_printf(&b, "ppm = %d\n", o->ppm);
    fc_buf_printf(&b, "tex = %d\n", o->tex);
    fc_buf_printf(&b, "bg = %06X\n", (unsigned)o->bg);
    fc_buf_printf(&b, "out = %s\n", o->out ? o->out : "-");
    fc_buf_printf(&b, "file = %s\n", o->file ? o->file : "-");
    for (i = 0; i < o->nlibdirs; i++) fc_buf_printf(&b, "lib_dir = %s\n", o->libdirs[i]);
    for (i = 0; i < o->nlibs; i++)
        fc_buf_printf(&b, "lib = %s%s%s\n", o->libs[i], o->lib_prefixes[i] ? " prefix=" : "",
                      o->lib_prefixes[i] ? o->lib_prefixes[i] : "");
    for (i = 0; k_env[i]; i++) {
        const char *v = getenv(k_env[i]);
        if (v) fc_buf_printf(&b, "# env %s = %s\n", k_env[i], v);
    }
    fc_buf_printf(&b, "macros_loaded = %d\n", fc_lib_count(lib));
    {
        char exe[4096];
        fc_buf_printf(&b, "# exe = %s\n", fc_exe_path(exe, sizeof exe) ? exe : "(unknown)");
    }
    fc_buf_printf(&b, "# fonts = %s\n", fc_ttf_init() ? "ttf" : "stroke (no Liberation TTF found)");
    {
        int rc = write_out(o, b.p ? b.p : "", b.n);
        fc_buf_free(&b);
        return rc;
    }
}

static int edit_drawing(const Opt *o, FcDrawing *d) {
    FcPrim *p;
    if (!o->action) return FC_ERR_ARG;
    if (fc_streq(o->action, "add")) {
        FcDrawing extra;
        char *text = NULL;
        FcEmitOpts emit = {1, 1};
        int i, rc;
        if (!o->from) return FC_ERR_ARG;
        fc_drawing_init(&extra);
        rc = fc_parse_file(&extra, o->from);
        for (i = 0; i < extra.ndiags; i++)
            if (extra.diags[i].level == FC_DIAG_ERR) rc = FC_ERR_ARG;
        if (rc == FC_OK) {
            size_t n = fc_emit(&extra, &text, &emit);
            if (!text) rc = FC_ERR_NOMEM;
            else fc_append_fcd(d, text, n);
        }
        free(text); fc_drawing_free(&extra); return rc;
    }
    if (fc_streq(o->action, "layer")) {
        FcLayer *l;
        if (o->layer < 0 || (!o->reset && !o->has_color && o->alpha < 0 && !o->name)) return FC_ERR_ARG;
        l = &d->layers[o->layer];
        if (o->reset) {
            snprintf(l->name, sizeof l->name, "%s", fc_layer_default_name(o->layer));
            l->rgb = fc_layer_default_rgb(o->layer);
            l->alpha = fc_layer_default_alpha(o->layer);
            l->modified = 0;
        }
        if (o->has_color) l->rgb = o->color;
        if (o->alpha >= 0) l->alpha = (float)o->alpha;
        if (o->name) {
            if (!o->name[0] || strlen(o->name) >= sizeof l->name || strchr(o->name, '\n')) return FC_ERR_ARG;
            snprintf(l->name, sizeof l->name, "%s", o->name);
        }
        if (o->has_color || o->alpha >= 0 || o->name) l->modified = 1;
        return FC_OK;
    }
    if (o->index < 0 || o->index >= d->nprims) return FC_ERR_ARG;
    p = &d->prims[o->index];
    if (fc_streq(o->action, "move")) fc_prim_translate(p, o->dx, o->dy);
    else if (fc_streq(o->action, "rm")) fc_drawing_remove(d, o->index);
    else if (fc_streq(o->action, "set")) {
        if (o->layer >= 0) p->layer = o->layer;
        if (o->dash >= 0 || o->stroke >= 0) {
            if (!(fc_kind_caps(p->kind) & FC_KIND_DASH)) return FC_ERR_ARG;
            if (o->dash >= 0) p->ext.dash = o->dash;
            if (o->stroke >= 0) p->ext.width = o->stroke;
            p->has_ext = 1;
        }
        if (o->name && fc_prim_set_text(d, o->index, 1, o->name) != FC_OK) return FC_ERR_ARG;
        if (o->value && fc_prim_set_text(d, o->index, 2, o->value) != FC_OK) return FC_ERR_ARG;
        if (o->text && fc_prim_set_text(d, o->index, 0, o->text) != FC_OK) return FC_ERR_ARG;
    } else return FC_ERR_ARG;
    return FC_OK;
}

static int query_drawing(const Opt *o, const FcDrawing *d, const FcLib *lib) {
    FcBuf b;
    int i, n = 0;
    fc_buf_init(&b);
    if (fc_streq(o->cmd, "hit")) {
        if (!o->has_at) return FC_ERR_ARG;
        fc_buf_printf(&b, "{\"index\":%d}\n", fc_hit_test(d, lib, o->x, o->y, o->tolerance));
    } else {
        FcRect *r = (FcRect *)calloc((size_t)d->nprims + 1, sizeof *r);
        int *has = (int *)calloc((size_t)d->nprims + 1, sizeof *has);
        if (!o->has_box || !r || !has) {
            free(r);
            free(has);
            return o->has_box ? FC_ERR_NOMEM : FC_ERR_ARG;
        }
        fc_prims_bounds(d, lib, r, has);
        fc_buf_puts(&b, "{\"indices\":[");
        for (i = 0; i < d->nprims; i++) {
            if (has[i] && r[i].min_x <= o->box.max_x && r[i].max_x >= o->box.min_x &&
                r[i].min_y <= o->box.max_y && r[i].max_y >= o->box.min_y) {
                fc_buf_printf(&b, "%s%d", n++ ? "," : "", i);
            }
        }
        fc_buf_puts(&b, "]}\n");
        free(r);
        free(has);
    }
    i = write_out(o, b.p, b.n); fc_buf_free(&b); return i;
}

static int cmd_macro(const Opt *o, FcLib *lib) {
    FcDrawing d, expanded;
    FcPrim *p;
    char *json, key[512];
    int rc;
    if (!o->file || strlen(o->file) >= sizeof key) return FC_ERR_ARG;
    snprintf(key, sizeof key, "%s", o->file); fc_tolower_buf(key);
    if (!fc_lib_find(lib, key)) { fprintf(stderr, "fcai: unknown macro %s\n", key); return FC_ERR_ARG; }
    fc_drawing_init(&d); fc_drawing_init(&expanded);
    p = fc_drawing_push(&d);
    if (!p) { fc_drawing_free(&d); fc_drawing_free(&expanded); return FC_ERR_NOMEM; }
    p->kind = FC_MACRO; p->u.macro.key = key;
    p->u.macro.p.x = o->has_at ? o->x : 100;
    p->u.macro.p.y = o->has_at ? o->y : 100;
    p->u.macro.orient = o->orient; p->u.macro.mirror = o->mirror;
    fc_expand(&d, &expanded, lib);
    json = fc_to_json(&expanded, NULL);
    rc = json ? write_out(o, json, strlen(json)) : FC_ERR_NOMEM;
    free(json); fc_drawing_free(&d); fc_drawing_free(&expanded); return rc;
}

static int run_on_drawing(const Opt *o, FcDrawing *d, FcLib *lib) {
    const char *cmd = o->cmd[0] ? o->cmd : "context";
    FcDescribeOpts dopts;
    FcEmitOpts eopts;
    char *s = NULL;
    int rc = FC_OK;
    dopts.verbose = o->verbose || fc_streq(cmd, "context");
    eopts.extensions = o->extensions;
    eopts.header = 1;
    if (fc_streq(cmd, "edit")) {
        rc = edit_drawing(o, d);
        if (rc != FC_OK) { fprintf(stderr, "fcai: invalid edit action, index, or fields\n"); return rc; }
        size_t n = fc_emit(d, &s, &eopts);
        rc = s ? write_out(o, s, n) : FC_ERR_NOMEM;
        free(s); return rc;
    }
    if (fc_streq(cmd, "hit") || fc_streq(cmd, "find")) return query_drawing(o, d, lib);
    if (fc_streq(cmd, "lint")) {
        s = fc_lint(d, lib, o->json);
        rc = s ? write_out(o, s, strlen(s)) : FC_ERR_NOMEM;
        free(s); return rc;
    }
    check_macros(d, lib);

    if (fc_streq(cmd, "context")) {
        s = o->json ? fc_to_json(d, lib) : fc_context(d, lib);
        if (s && !o->json) {
            FcBuf b;
            fc_buf_init(&b);
            /* Keep fc_context and drawing JSON independent of user state. */
            if (fc_style_context(&b) != FC_OK && !o->quiet)
                fprintf(stderr, "fcai: warning: style profile unavailable; use `fcai style show`\n");
            fc_buf_puts(&b, s);
            rc = write_out(o, b.p ? b.p : "", b.n);
            fc_buf_free(&b);
        } else rc = s ? write_out(o, s, fc_strlen(s)) : FC_ERR_NOMEM;
        free(s);
        return rc;
    }
    if (fc_streq(cmd, "describe")) {
        s = fc_describe(d, lib, &dopts);
        rc = s ? write_out(o, s, fc_strlen(s)) : FC_ERR_NOMEM;
        free(s);
        return rc;
    }
    if (fc_streq(cmd, "json") || fc_streq(cmd, "dump") || fc_streq(cmd, "parse")) {
        s = fc_to_json(d, lib);
        rc = s ? write_out(o, s, fc_strlen(s)) : FC_ERR_NOMEM;
        free(s);
        return rc;
    }
    if (fc_streq(cmd, "emit")) {
        size_t n = fc_emit(d, &s, &eopts);
        rc = s ? write_out(o, s, n) : FC_ERR_NOMEM;
        free(s);
        return rc;
    }
    if (fc_streq(cmd, "validate")) {
        int v = fc_validate(d, &s);
        rc = s ? write_out(o, s, fc_strlen(s)) : FC_ERR_NOMEM;
        free(s);
        return v == FC_OK ? rc : 1;
    }
    if (fc_streq(cmd, "expand")) {
        FcDrawing exp;
        size_t n;
        fc_drawing_init(&exp);
        fc_expand(d, &exp, lib);
        n = fc_emit(&exp, &s, &eopts);
        rc = s ? write_out(o, s, n) : FC_ERR_NOMEM;
        free(s);
        fc_drawing_free(&exp);
        return rc;
    }
    if (fc_streq(cmd, "render")) {
        FcRenderOpts ro;
        fc_render_opts_init(&ro);
        ro.has_crop = o->has_crop;
        ro.crop = o->crop;
        ro.scale = o->scale;
        ro.margin = o->margin;
        ro.max_w = o->max_w;
        ro.max_h = o->max_h;
        ro.grid = o->grid;
        ro.bg = o->bg;
        ro.ppm = o->ppm;
        ro.tex = o->tex;
        return fc_render_file(d, lib, &ro, o->out ? o->out : "-");
    }
    fprintf(stderr, "fcai: unknown command '%s'\n", cmd);
    return FC_ERR_ARG;
}

static int cmd_diff(const Opt *o) {
    FcDrawing a, b;
    char *s;
    int rc;
    if (!o->file || !o->file2) {
        fprintf(stderr, "fcai: diff needs two files\n");
        return FC_ERR_ARG;
    }
    fc_drawing_init(&a);
    fc_drawing_init(&b);
    if (fc_parse_file(&a, o->file) != FC_OK || fc_parse_file(&b, o->file2) != FC_OK) {
        fprintf(stderr, "fcai: cannot read %s or %s\n", o->file, o->file2);
        fc_drawing_free(&a);
        fc_drawing_free(&b);
        return FC_ERR_IO;
    }
    s = fc_diff(&a, &b, o->json);
    rc = s ? write_out(o, s, fc_strlen(s)) : FC_ERR_NOMEM;
    free(s);
    fc_drawing_free(&a);
    fc_drawing_free(&b);
    return rc;
}

/* ---- fcai session: talk to a running fcai-gui -------------------------- */

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void default_session_dir(const char *file, char *out, size_t n) {
    const char *slash = strrchr(file, '/');
    if (slash) {
        int dlen = (int)(slash - file);
        snprintf(out, n, "%.*s/.%s.fcai-session", dlen, file, slash + 1);
    } else {
        snprintf(out, n, "./.%s.fcai-session", file);
    }
}

static int session_append(const char *dir, FcBuf *b) {
    char path[4096];
    FILE *f;
    snprintf(path, sizeof path, "%s/commands.jsonl", dir);
    f = fopen(path, "ab");
    if (!f) {
        fprintf(stderr, "fcai: cannot write %s (is fcai-gui running?)\n", path);
        return FC_ERR_IO;
    }
    if (fwrite(b->p ? b->p : "", 1, b->n, f) != b->n) {
        fclose(f);
        return FC_ERR_IO;
    }
    fclose(f);
    return FC_OK;
}

/* Read complete event lines without a fixed drawing-size limit. */
static int session_wait(const char *dir, int timeout_s) {
    char epath[4096], ppath[4096];
    long pos = 0;
    double waited = 0;
    FILE *pf;
    snprintf(epath, sizeof epath, "%s/events.jsonl", dir);
    snprintf(ppath, sizeof ppath, "%s/.events.pos", dir);
    pf = fopen(ppath, "rb");
    if (pf) {
        if (fscanf(pf, "%ld", &pos) != 1 || pos < 0) pos = 0;
        fclose(pf);
    }
    for (;;) {
        FILE *f = fopen(epath, "rb");
        if (f) {
            char *line = NULL;
            size_t cap = 0;
            ssize_t n;
            if (fseek(f, 0, SEEK_END) == 0 && ftell(f) < pos) pos = 0;
            if (fseek(f, pos, SEEK_SET) == 0 && (n = getline(&line, &cap, f)) > 0 &&
                line[n - 1] == '\n') {
                pos = ftell(f);
                fclose(f);
                fwrite(line, 1, (size_t)n, stdout);
                free(line);
                pf = fopen(ppath, "wb");
                if (pf) { fprintf(pf, "%ld", pos); fclose(pf); }
                return 0;
            }
            free(line);
            fclose(f);
        }
        if (timeout_s > 0 && waited >= timeout_s) {
            fprintf(stderr, "fcai: session wait: timeout after %d s\n", timeout_s);
            return 3;
        }
        { struct timespec ts = {0, 100000000}; nanosleep(&ts, NULL); }
        waited += 0.1;
    }
}

static int session_status(const char *dir) {
    char path[4096], *line = NULL;
    size_t cap = 0;
    long pos = 0, pending = 0, changes = 0;
    ssize_t n;
    FILE *f;
    snprintf(path, sizeof path, "%s/.events.pos", dir);
    f = fopen(path, "rb");
    if (f) { if (fscanf(f, "%ld", &pos) != 1 || pos < 0) pos = 0; fclose(f); }
    snprintf(path, sizeof path, "%s/events.jsonl", dir);
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "fcai: cannot read %s\n", path); return 1; }
    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) < pos) pos = 0;
    if (fseek(f, pos, SEEK_SET) != 0) { fclose(f); return 1; }
    while ((n = getline(&line, &cap, f)) > 0) {
        if (line[n - 1] != '\n') break;
        pending++;
        /* GUI events are compact JSON; FCD text quotes are escaped. */
        if (strstr(line, "\"event\":\"change\"")) changes++;
    }
    free(line);
    fclose(f);
    printf("{\"pending\":%ld,\"pending_changes\":%ld,\"read_offset\":%ld}\n", pending, changes, pos);
    return 0;
}

static int session_reply(const char *dir, const char *id, int timeout_s, int print_reply) {
    char path[4096];
    double waited = 0;
    snprintf(path, sizeof path, "%s/replies/%s.json", dir, id);
    for (;;) {
        char *reply = NULL;
        size_t n = 0;
        if (file_exists(path) && fc_read_file(path, &reply, &n) == FC_OK) {
            int ok = strstr(reply, "\"ok\":true") != NULL;
            if (!print_reply) ok = ok && strstr(reply, "\"protocol\":2") != NULL;
            if (print_reply) fwrite(reply, 1, n, stdout);
            free(reply);
            unlink(path);
            return ok ? 0 : 1;
        }
        if (waited >= timeout_s) {
            fprintf(stderr, "fcai: session reply: timeout after %d s (command may still apply)\n", timeout_s);
            return 3;
        }
        { struct timespec ts = {0, 100000000}; nanosleep(&ts, NULL); }
        waited += 0.1;
    }
}

static void session_usage(void) {
    fputs("usage: fcai session <command> DIR [args]\n"
          "  status DIR                  pending event counts (does not consume)\n"
          "  wait DIR [--timeout SECS]    print the next event line from the GUI\n"
          "  push DIR [FILE] [--base FCD] replace drawing; reject if base changed\n"
          "  set DIR KEY VALUE           change a gui.conf setting live\n"
          "  get DIR                     request full state\n"
          "  save DIR                    save the current GUI drawing\n"
          "  reload DIR [--force]        reload disk (refuses unsaved GUI edits)\n"
          "  reload-libs DIR             reload libraries without restarting\n"
          "  ack DIR [TEXT]              acknowledge the user's changes\n"
          "  quit DIR                    ask the GUI to close (Done)\n"
          "  path FILE                   print the default session dir\n"
          "get/push/save/reload/reload-libs: --wait [--timeout SECS] prints a\n"
          "correlated reply without consuming pending events (default 5 s).\n",
          stderr);
}

static int cmd_session(int argc, char **argv) {
    const char *sub = argc > 1 ? argv[1] : NULL, *dir;
    const char *args[3], *base = NULL;
    int nargs = 0, wait_reply = 0, timeout = 5, force = 0, i, rc;
    char id[100] = "";
    FcBuf b;
    if (!sub || fc_streq(sub, "-h") || fc_streq(sub, "--help") || fc_streq(sub, "help")) {
        session_usage(); return sub ? 0 : 1;
    }
    if (argc < 3) { session_usage(); return 1; }
    dir = argv[2];
    if (fc_streq(sub, "path")) {
        char out[4096]; default_session_dir(dir, out, sizeof out); puts(out); return 0;
    }
    if (fc_streq(sub, "wait")) timeout = 0;
    for (i = 3; i < argc; i++) {
        if (fc_streq(argv[i], "--wait")) wait_reply = 1;
        else if (fc_streq(argv[i], "--force")) force = 1;
        else if (fc_streq(argv[i], "--base") && i + 1 < argc) base = argv[++i];
        else if (fc_streq(argv[i], "--timeout") && i + 1 < argc) {
            if (!fc_parse_int(argv[++i], &timeout) || timeout <= 0) {
                fprintf(stderr, "fcai: timeout must be a positive integer\n"); return 1;
            }
        } else if (nargs < 3 && (argv[i][0] != '-' || fc_streq(argv[i], "-"))) args[nargs++] = argv[i];
        else { session_usage(); return 1; }
    }
    if (base && !fc_streq(sub, "push")) { session_usage(); return 1; }
    if (force && !fc_streq(sub, "reload")) { session_usage(); return 1; }
    if (fc_streq(sub, "wait") && !nargs && !wait_reply) return session_wait(dir, timeout);
    if (fc_streq(sub, "status") && !nargs && !wait_reply) return session_status(dir);
    if (wait_reply && !(fc_streq(sub, "get") || fc_streq(sub, "push") ||
                       fc_streq(sub, "save") || fc_streq(sub, "reload") || fc_streq(sub, "reload-libs"))) {
        session_usage(); return 1;
    }
    fc_buf_init(&b);
    fc_buf_puts(&b, "{\"cmd\":"); fc_json_escape(&b, sub);
    if (wait_reply || base) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        snprintf(id, sizeof id, "%ld-%ld-%ld", (long)getpid(), (long)ts.tv_sec, ts.tv_nsec);
        if (wait_reply) { fc_buf_puts(&b, ",\"id\":"); fc_json_escape(&b, id); }
    }
    if (fc_streq(sub, "push") && nargs <= 1) {
        char *content = NULL; size_t len = 0;
        if (fc_read_file(nargs ? args[0] : "-", &content, &len) != FC_OK) goto bad_file;
        fc_buf_puts(&b, ",\"fcd\":"); fc_json_escape(&b, content); free(content);
        if (base) {
            if (fc_read_file(base, &content, &len) != FC_OK) goto bad_file;
            fc_buf_puts(&b, ",\"base\":"); fc_json_escape(&b, content); free(content);
        }
    } else if (fc_streq(sub, "set") && nargs == 2) {
        fc_buf_puts(&b, ",\"key\":"); fc_json_escape(&b, args[0]);
        fc_buf_puts(&b, ",\"value\":"); fc_json_escape(&b, args[1]);
    } else if (fc_streq(sub, "ack") && nargs <= 1) {
        fc_buf_puts(&b, ",\"text\":"); fc_json_escape(&b, nargs ? args[0] : "");
    } else if (!nargs && (fc_streq(sub, "get") || fc_streq(sub, "quit") ||
                         fc_streq(sub, "save") || fc_streq(sub, "reload") || fc_streq(sub, "reload-libs"))) {
        if (force) fc_buf_puts(&b, ",\"force\":true");
    } else { session_usage(); fc_buf_free(&b); return 1; }
    fc_buf_puts(&b, "}\n");
    if (base) {
        /* Old GUIs ignore unknown fields. Verify support before sending a
         * guarded mutation, so --base can never degrade to an unguarded push. */
        FcBuf probe;
        char probe_id[120];
        snprintf(probe_id, sizeof probe_id, "%s-probe", id);
        fc_buf_init(&probe);
        fc_buf_puts(&probe, "{\"cmd\":\"get\",\"id\":");
        fc_json_escape(&probe, probe_id); fc_buf_puts(&probe, "}\n");
        rc = session_append(dir, &probe);
        fc_buf_free(&probe);
        if (rc == FC_OK) rc = session_reply(dir, probe_id, timeout, 0);
        if (rc != 0) {
            fprintf(stderr, "fcai: guarded push not sent; need a running GUI with protocol 2 (reopen older windows)\n");
            fc_buf_free(&b); return rc;
        }
    }
    rc = session_append(dir, &b);
    fc_buf_free(&b);
    if (rc != FC_OK) return 1;
    return wait_reply ? session_reply(dir, id, timeout, 1) : 0;
bad_file:
    fprintf(stderr, "fcai: cannot read push/base file\n");
    fc_buf_free(&b);
    return 1;
}

/* ---- fcai skill: the fidocad agent skill ------------------------------- */

/* The skill directory holds SKILL.md. Locate it through sys.c the same way
 * libraries and fonts are found, so installed Linux and macOS trees both work
 * (/usr/local, /opt/homebrew, /usr/share, or next to the binary). */
typedef struct {
    char *out;
    size_t cap;
} SkillPath;

static int try_skill_md(void *ctx, const char *dir) {
    SkillPath *sp = (SkillPath *)ctx;
    snprintf(sp->out, sp->cap, "%s/SKILL.md", dir);
    return file_exists(sp->out);
}

static int find_skill_file(char *out, size_t n) {
    const char *env = getenv("FIDO_SKILL_DIR");
    char cand[4096];
    SkillPath sp;
    if (env && *env) {
        /* parent of fidocad/ (the installed share/fidocad/skills), or the skill
         * directory itself */
        snprintf(cand, sizeof cand, "%s/fidocad/SKILL.md", env);
        if (file_exists(cand)) {
            snprintf(out, n, "%s", cand);
            return 1;
        }
        snprintf(cand, sizeof cand, "%s/SKILL.md", env);
        if (file_exists(cand)) {
            snprintf(out, n, "%s", cand);
            return 1;
        }
    }
    sp.out = out;
    sp.cap = n;
    return fc_find_data("skills/fidocad", try_skill_md, &sp);
}

/* Canonical source for `fcai skill install`: the Omarchy default-agents
   channel when the skill is installed there ($FIDO_SKILL_DIR overrides),
   otherwise the skill directory next to the binary. Agent directories get
   symlinks to this directory — like the Omarchy default skills — so every
   agent sees one skill and pi deduplicates the entries by real path. */
static int find_skill_install_dir(char *out, size_t n) {
    const char *env = getenv("FIDO_SKILL_DIR");
    const char *omarchy = getenv("OMARCHY_PATH");
    char file[4096];
    char cand[3072];
    size_t k, dcut = 0;
    if (env && *env) {
        snprintf(cand, sizeof cand, "%s/fidocad", env);
        snprintf(file, sizeof file, "%s/SKILL.md", cand);
        if (file_exists(file)) { snprintf(out, n, "%s", cand); return 1; }
    }
    snprintf(cand, sizeof cand, "%s/default/agents/skills/fidocad",
             (omarchy && *omarchy) ? omarchy : "/usr/share/omarchy");
    snprintf(file, sizeof file, "%s/SKILL.md", cand);
    if (file_exists(file)) { snprintf(out, n, "%s", cand); return 1; }
    if (!find_skill_file(file, sizeof file)) return 0;
    for (k = fc_strlen(file); k > 0; k--) {
        if (file[k - 1] == '/') { dcut = k - 1; break; }
    }
    if (dcut >= n) dcut = n - 1;
    memcpy(out, file, dcut);
    out[dcut] = 0;
    if (dcut == 0) snprintf(out, n, ".");
    return 1;
}

/* Make a path absolute and lexically remove . and .. segments (never
   resolving symlinks), so an Omarchy channel symlink stays visible in the
   links we create while the target does not depend on `build/` surviving. */
static void make_abs(char *path, size_t cap) {
    char cwd[3072];
    char tmp[4096];
    char norm[4096];
    char *parts[256];
    int n = 0, i;
    char *p;
    size_t o = 0;
    if (path[0] != '/') {
        if (!getcwd(cwd, sizeof cwd)) return;
        if (snprintf(tmp, sizeof tmp, "%s/%s", cwd, path) >= (int)sizeof tmp) return;
    } else {
        snprintf(tmp, sizeof tmp, "%s", path);
    }
    p = tmp;
    while (*p) {
        char *seg;
        while (*p == '/') p++;
        if (!*p) break;
        seg = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = 0;
        if (fc_streq(seg, ".")) continue;
        if (fc_streq(seg, "..")) {
            if (n > 0) n--;
            continue;
        }
        if (n < 256) parts[n++] = seg;
    }
    if (n == 0) {
        snprintf(path, cap, "/");
        return;
    }
    for (i = 0; i < n; i++) {
        int m = snprintf(norm + o, sizeof norm - o, "/%s", parts[i]);
        if (m < 0 || (size_t)m >= sizeof norm - o) break;
        o += (size_t)m;
    }
    snprintf(path, cap, "%s", norm);
}

static int mkdir_p(const char *path) {
    char tmp[4096];
    char *p;
    size_t len = fc_strlen(path);
    if (len == 0 || len >= sizeof tmp) return 0;
    memcpy(tmp, path, len + 1);
    for (p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return 0;
        *p = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return 0;
    return 1;
}

static int rm_rf(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *dp = opendir(path);
        struct dirent *de;
        if (dp) {
            while ((de = readdir(dp))) {
                char child[4096];
                if (fc_streq(de->d_name, ".") || fc_streq(de->d_name, "..")) continue;
                if (snprintf(child, sizeof child, "%s/%s", path, de->d_name) >= (int)sizeof child)
                    continue;
                rm_rf(child);
            }
            closedir(dp);
        }
        return rmdir(path);
    }
    return unlink(path);
}

/* Where locally installed coding agents keep their skills. The first four
   are always written; the rest only when that agent already has a config
   directory, so we never invent settings trees for agents the user does
   not run. */
static const struct {
    const char *parent; /* relative to $HOME */
    const char *skills;
    int always;
} k_skill_agents[] = {
    {".agents", "skills", 1},        /* cross-client Agent Skills convention */
    {".config/agents", "skills", 1},  /* generic */
    {".pi", "agent/skills", 1},
    {".claude", "skills", 1},
    {".codex", "skills", 0},
    {".gemini", "skills", 0},
    {".gemini/config", "skills", 0},
    {".config/opencode", "skills", 0},
    {".config/crush", "skills", 0},
    {".config/zed", "skills", 0},
    {".cursor", "skills", 0},
    {".hermes/skills", "note-taking", 0}, /* Hermes files skills by category */
    {NULL, NULL, 0},
};

/* Skills directories to act on: the ones given (or $FIDO_SKILL_DIRS, colon
   separated), else the agents above; installing skips absent optional ones. */
static int skill_dirs(int argc, char **argv, int installing, char dirs[][4096], int max) {
    const char *home = getenv("HOME"), *list = getenv("FIDO_SKILL_DIRS");
    int i, n = 0;
    for (i = 0; i < argc && n < max; i++) snprintf(dirs[n++], 4096, "%s", argv[i]);
    if (argc > 0) return n;
    if (list && *list) {
        while (*list && n < max) {
            size_t len = strcspn(list, ":");
            if (len) snprintf(dirs[n++], 4096, "%.*s", (int)len, list);
            list += len + (list[len] == ':');
        }
        return n;
    }
    if (!home || !*home) {
        fprintf(stderr, "fcai: HOME is not set; pass target directories\n");
        return -1;
    }
    for (i = 0; k_skill_agents[i].parent && n < max; i++) {
        char parent[3072];
        struct stat st;
        snprintf(parent, sizeof parent, "%s/%s", home, k_skill_agents[i].parent);
        if (installing && !k_skill_agents[i].always && stat(parent, &st) != 0) continue;
        snprintf(dirs[n++], 4096, "%s/%s", parent, k_skill_agents[i].skills);
    }
    return n;
}

/* dir/name into out; 0 when it does not fit. */
static int path_join(char *out, size_t cap, const char *dir, const char *name) {
    int n = snprintf(out, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

/* An entry fcai may replace or remove: a link (dangling or not) or a copy made
   by an older installer, which holds SKILL.md. Anything else is left alone. */
static int is_fidocad_skill(const char *dst, const struct stat *st) {
    char skill[4200];
    if (S_ISLNK(st->st_mode)) return 1;
    return S_ISDIR(st->st_mode) && path_join(skill, sizeof skill, dst, "SKILL.md") &&
           file_exists(skill);
}

static int skill_install(const char *srcdir, int argc, char **argv) {
    char dirs[16][4096];
    int nd = skill_dirs(argc, argv, 1, dirs, 16), i, rc = 0;
    if (nd < 0) return 1;
    for (i = 0; i < nd; i++) {
        char dst[4200];
        struct stat st;
        if (!path_join(dst, sizeof dst, dirs[i], "fidocad")) continue;
        if (!mkdir_p(dirs[i])) {
            fprintf(stderr, "fcai: cannot create %s\n", dirs[i]);
            rc = 1;
            continue;
        }
        if (lstat(dst, &st) == 0) {
            if (!is_fidocad_skill(dst, &st)) {
                fprintf(stderr, "fcai: %s exists and is not a fidocad skill; leaving it\n", dst);
                rc = 1;
                continue;
            }
            rm_rf(dst); /* a stale link, or a copy made by an older installer */
        }
        if (symlink(srcdir, dst) != 0) {
            fprintf(stderr, "fcai: cannot link %s -> %s\n", dst, srcdir);
            rc = 1;
            continue;
        }
        printf("linked %s -> %s\n", dst, srcdir);
    }
    return rc;
}

/* uninstall removes what install made; status prints each working install. */
static int skill_scan(int uninstall, int argc, char **argv) {
    char dirs[16][4096];
    int nd = skill_dirs(argc, argv, 0, dirs, 16), i, rc = 0;
    if (nd < 0) return 1;
    for (i = 0; i < nd; i++) {
        char dst[4200], skill[4300];
        struct stat st;
        if (!path_join(dst, sizeof dst, dirs[i], "fidocad") ||
            !path_join(skill, sizeof skill, dst, "SKILL.md"))
            continue;
        if (!uninstall) {
            if (file_exists(skill)) printf("%s\n", dst);
        } else if (lstat(dst, &st) == 0 && is_fidocad_skill(dst, &st)) {
            if (rm_rf(dst) == 0) {
                printf("removed %s\n", dst);
            } else {
                fprintf(stderr, "fcai: cannot remove %s\n", dst);
                rc = 1;
            }
        }
    }
    return rc;
}

static int cmd_skill(int argc, char **argv) {
    char path[4096];
    const char *sub = argc > 0 ? argv[0] : "show";
    if (fc_streq(sub, "-h") || fc_streq(sub, "--help") || fc_streq(sub, "help")) {
        fputs("usage: fcai skill [show|path]\n"
              "       fcai skill install [DIR...]    symlinks the skill into each agent's\n"
              "                                      skills dir (default: ~/.agents/skills,\n"
              "                                      ~/.pi, ~/.claude, ~/.codex, ...)\n"
              "       fcai skill uninstall [DIR...]  removes those links\n"
              "       fcai skill status [DIR...]     lists where the skill is installed\n"
              "FIDO_SKILL_DIRS (colon separated) replaces the default directories.\n",
              stdout);
        return 0;
    }
    if (fc_streq(sub, "uninstall")) return skill_scan(1, argc - 1, argv + 1);
    if (fc_streq(sub, "status")) return skill_scan(0, argc - 1, argv + 1);
    if (fc_streq(sub, "install")) {
        char dir[4096];
        if (!find_skill_install_dir(dir, sizeof dir)) {
            fprintf(stderr, "fcai: fidocad skill not found (set FIDO_SKILL_DIR)\n");
            return 1;
        }
        make_abs(dir, sizeof dir);
        return skill_install(dir, argc - 1, argv + 1);
    }
    if (!find_skill_file(path, sizeof path)) {
        fprintf(stderr, "fcai: fidocad skill not found (set FIDO_SKILL_DIR)\n");
        return 1;
    }
    if (fc_streq(sub, "show") || fc_streq(sub, "print")) {
        char *text = NULL;
        size_t len = 0;
        if (fc_read_file(path, &text, &len) != FC_OK) return 1;
        fwrite(text, 1, len, stdout);
        free(text);
        return 0;
    }
    if (fc_streq(sub, "path")) {
        printf("%s\n", path);
        return 0;
    }
    fprintf(stderr, "fcai: unknown skill command '%s'\n", sub);
    return 1;
}

/* `fcai gui ...` hands off to fcai-gui, looked up next to this binary first. */
static int exec_gui(const char *argv0, int argc, char **argv) {
    char path[4096];
    const char *slash = strrchr(argv0, '/');
    argv[0] = "fcai-gui";
    if (slash && (size_t)(slash - argv0) + 10 < sizeof path) {
        memcpy(path, argv0, (size_t)(slash - argv0) + 1);
        strcpy(path + (slash - argv0) + 1, "fcai-gui");
        execv(path, argv);
    }
    execvp("fcai-gui", argv);
    fprintf(stderr, "fcai: cannot run fcai-gui (build it with `make gui`)\n");
    (void)argc;
    return 127;
}

int main(int argc, char **argv) {
    Opt o;
    FcLib *lib;
    FcDrawing d;
    int i, rc;

    if (argc > 1 && fc_streq(argv[1], "gui")) return exec_gui(argv[0], argc - 1, argv + 1);
    if (argc > 1 && fc_streq(argv[1], "session")) return cmd_session(argc - 1, argv + 1);
    if (argc > 1 && fc_streq(argv[1], "skill")) return cmd_skill(argc - 2, argv + 2);
    if (argc > 1 && fc_streq(argv[1], "style")) return fc_cmd_style(argc - 2, argv + 2);

    opt_init(&o);
    fc_set_argv0(argv[0]);

    /* parse argv: flags anywhere, first non-flag is command if it matches, else file */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        /* the words help/version count only in the command slot, so a file may use them */
        if (fc_streq(a, "-h") || fc_streq(a, "--help") || (!o.cmd[0] && fc_streq(a, "help"))) {
            fputs(k_help, stdout);
            return 0;
        }
        if (fc_streq(a, "--version") || (!o.cmd[0] && fc_streq(a, "version"))) {
            printf("%s\n", FC_VERSION);
            return 0;
        }
        if (fc_streq(a, "--stdlib")) {
            o.stdlib = 1;
            continue;
        }
        if (fc_streq(a, "--no-stdlib")) {
            o.stdlib = 0;
            continue;
        }
        if (fc_streq(a, "--extensions")) {
            o.extensions = 1;
            continue;
        }
        if (fc_streq(a, "--no-extensions")) {
            o.extensions = 0;
            continue;
        }
        if (fc_streq(a, "--json")) {
            o.json = 1;
            continue;
        }
        if (fc_streq(a, "-q") || fc_streq(a, "--quiet")) {
            o.quiet = 1;
            continue;
        }
        if (fc_streq(a, "-v") || fc_streq(a, "--verbose")) {
            o.verbose = 1;
            continue;
        }
        if (fc_streq(a, "--grid")) {
            o.grid = 1;
            continue;
        }
        if (fc_streq(a, "--ppm")) {
            o.ppm = 1;
            continue;
        }
        if (fc_streq(a, "--tex")) {
            o.tex = 1;
            continue;
        }
        if (fc_streq(a, "--no-tex")) {
            o.tex = 0;
            continue;
        }
        if ((fc_streq(a, "-o") || fc_streq(a, "--out")) && i + 1 < argc) {
            o.out = argv[++i];
            continue;
        }
        if (fc_streq(a, "--lib") && i + 1 < argc) {
            add_lib(&o, argv[++i]);
            continue;
        }
        if (fc_streq(a, "--lib-dir") && i + 1 < argc) {
            if (o.nlibdirs < 16) o.libdirs[o.nlibdirs++] = argv[++i];
            continue;
        }
        if (fc_streq(a, "--prefix") && i + 1 < argc) {
            snprintf(o.prefix, sizeof o.prefix, "%s", argv[++i]);
            continue;
        }
        if (fc_streq(a, "--config") && i + 1 < argc) {
            o.config_path = argv[++i];
            if (load_config(&o, o.config_path) != FC_OK) {
                fprintf(stderr, "fcai: cannot read config %s\n", o.config_path);
                return 1;
            }
            continue;
        }
        if (fc_streq(a, "--scale") && i + 1 < argc) {
            fc_parse_int(argv[++i], &o.scale);
            continue;
        }
        if (fc_streq(a, "--margin") && i + 1 < argc) {
            fc_parse_int(argv[++i], &o.margin);
            continue;
        }
        if (fc_streq(a, "--width") && i + 1 < argc) {
            fc_parse_int(argv[++i], &o.max_w);
            continue;
        }
        if (fc_streq(a, "--height") && i + 1 < argc) {
            fc_parse_int(argv[++i], &o.max_h);
            continue;
        }
        if (fc_streq(a, "--bg") && i + 1 < argc) {
            parse_bg(argv[++i], &o.bg);
            continue;
        }
        if ((fc_streq(a, "--crop") || fc_streq(a, "--bbox")) && i + 4 < argc) {
            FcRect *r = fc_streq(a, "--crop") ? &o.crop : &o.box;
            if (!fc_parse_double(argv[++i], &r->min_x) || !fc_parse_double(argv[++i], &r->min_y) ||
                !fc_parse_double(argv[++i], &r->max_x) || !fc_parse_double(argv[++i], &r->max_y) ||
                !isfinite(r->min_x) || !isfinite(r->min_y) || !isfinite(r->max_x) || !isfinite(r->max_y) ||
                r->min_x >= r->max_x || r->min_y >= r->max_y) {
                fprintf(stderr, "fcai: expected increasing finite bounds\n"); return 1;
            }
            if (fc_streq(a, "--crop")) o.has_crop = 1; else o.has_box = 1;
            continue;
        }
        if ((fc_streq(a, "--at") || fc_streq(a, "--by") || fc_streq(a, "--near")) && i + 2 < argc) {
            double x, y;
            if (!fc_parse_double(argv[++i], &x) || !fc_parse_double(argv[++i], &y) ||
                !isfinite(x) || !isfinite(y) || fabs(x) > FC_MAX_COORD || fabs(y) > FC_MAX_COORD) {
                fprintf(stderr, "fcai: invalid coordinates\n"); return 1;
            }
            if (fc_streq(a, "--by")) { o.dx = x; o.dy = y; }
            else { o.x = x; o.y = y; o.has_at = 1; }
            if (fc_streq(a, "--near")) {
                double radius;
                if (i + 1 >= argc || !fc_parse_double(argv[++i], &radius) || !isfinite(radius) || radius <= 0 || radius > FC_MAX_COORD) return 1;
                o.box = (FcRect){x - radius, y - radius, x + radius, y + radius}; o.has_box = 1;
            }
            continue;
        }
        if (fc_streq(a, "--stroke") && i + 1 < argc) {
            if (!fc_parse_double(argv[++i], &o.stroke) || !isfinite(o.stroke) || o.stroke < 0 ||
                o.stroke > FC_MAX_COORD) return 1;
            continue;
        }
        if (fc_streq(a, "--color") && i + 1 < argc) {
            const char *c = argv[++i];
            size_t n = strlen(c[0] == '#' ? c + 1 : c);
            if (n != 6 || strspn(c[0] == '#' ? c + 1 : c, "0123456789abcdefABCDEF") != 6 ||
                !parse_bg(c, &o.color)) {
                fprintf(stderr, "fcai: --color expects RRGGBB\n"); return 1;
            }
            o.has_color = 1;
            continue;
        }
        if (fc_streq(a, "--alpha") && i + 1 < argc) {
            if (!fc_parse_double(argv[++i], &o.alpha) || !isfinite(o.alpha) || o.alpha < 0 || o.alpha > 1) {
                fprintf(stderr, "fcai: --alpha expects 0..1\n"); return 1;
            }
            continue;
        }
        if (fc_streq(a, "--reset")) { o.reset = 1; continue; }
        if (fc_streq(a, "--tol") && i + 1 < argc) {
            if (!fc_parse_double(argv[++i], &o.tolerance) || !isfinite(o.tolerance) || o.tolerance < 0) return 1;
            continue;
        }
        if ((fc_streq(a, "--index") || fc_streq(a, "--layer") || fc_streq(a, "--dash") || fc_streq(a, "--orient")) && i + 1 < argc) {
            int n;
            if (!fc_parse_int(argv[++i], &n) || n < 0) return 1;
            if (fc_streq(a, "--index")) o.index = n;
            else if (fc_streq(a, "--layer") && n < FC_MAX_LAYERS) o.layer = n;
            else if (fc_streq(a, "--dash") && n <= 4) o.dash = n;
            else if (fc_streq(a, "--orient") && n <= 3) o.orient = n;
            else return 1;
            continue;
        }
        if (fc_streq(a, "--mirror")) { o.mirror = 1; continue; }
        if (fc_streq(a, "--from") && i + 1 < argc) { o.from = argv[++i]; continue; }
        if (fc_streq(a, "--name") && i + 1 < argc) { o.name = argv[++i]; continue; }
        if (fc_streq(a, "--value") && i + 1 < argc) { o.value = argv[++i]; continue; }
        if (fc_streq(a, "--text") && i + 1 < argc) { o.text = argv[++i]; continue; }
        if (a[0] == '-' && a[1] != 0) {
            fprintf(stderr, "fcai: unknown option %s\n", a);
            return 1;
        }
        if (!o.cmd[0] &&
            (fc_streq(a, "context") || fc_streq(a, "describe") || fc_streq(a, "json") ||
             fc_streq(a, "dump") || fc_streq(a, "parse") || fc_streq(a, "emit") ||
             fc_streq(a, "validate") || fc_streq(a, "render") || fc_streq(a, "expand") ||
             fc_streq(a, "lib") || fc_streq(a, "config") || fc_streq(a, "diff") ||
             fc_streq(a, "macro") || fc_streq(a, "lint") || fc_streq(a, "edit") ||
             fc_streq(a, "hit") || fc_streq(a, "find"))) {
            snprintf(o.cmd, sizeof o.cmd, "%s", a);
            continue;
        }
        if (fc_streq(o.cmd, "edit") && !o.action) { o.action = a; continue; }
        if (!o.file) {
            o.file = a;
            continue;
        }
        if (fc_streq(o.cmd, "diff") && !o.file2) {
            o.file2 = a;
            continue;
        }
        fprintf(stderr, "fcai: extra argument %s\n", a);
        return 1;
    }

    lib = fc_lib_new();
    if (!lib) return 1;
    if (fc_lib_load_all(lib, o.stdlib, o.libdirs, o.nlibdirs, o.libs, o.lib_prefixes, o.nlibs,
                        lib_report, &o) != FC_OK) {
        fc_lib_free(lib);
        return 1;
    }

    if (fc_streq(o.cmd, "macro")) {
        rc = cmd_macro(&o, lib); fc_lib_free(lib); return rc == FC_OK ? 0 : 1;
    }
    if (fc_streq(o.cmd, "lib")) {
        rc = cmd_lib(&o, lib);
        fc_lib_free(lib);
        return rc == FC_OK ? 0 : 1;
    }
    if (fc_streq(o.cmd, "diff")) {
        fc_lib_free(lib);
        return cmd_diff(&o) == FC_OK ? 0 : 1;
    }
    if (fc_streq(o.cmd, "config")) {
        rc = cmd_config(&o, lib);
        fc_lib_free(lib);
        return rc == FC_OK ? 0 : 1;
    }

    /* an omitted file at a terminal would block on stdin; "-" still reads it explicitly */
    if (!o.file && fc_stdin_is_tty()) {
        fputs(k_help, stderr);
        fc_lib_free(lib);
        return 2;
    }

    fc_drawing_init(&d);
    rc = fc_parse_file(&d, o.file ? o.file : "-");
    if (rc != FC_OK) {
        fprintf(stderr, "fcai: failed to read %s\n", o.file ? o.file : "stdin");
        fc_drawing_free(&d);
        fc_lib_free(lib);
        return 1;
    }
    rc = run_on_drawing(&o, &d, lib);
    fc_drawing_free(&d);
    fc_lib_free(lib);
    return rc == FC_OK ? 0 : 1;
}
