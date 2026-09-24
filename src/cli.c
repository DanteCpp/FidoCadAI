#include "internal.h"

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
    "  config     Show effective settings\n"
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
    "  --scale N            Pixels per grid unit (default: auto)\n"
    "  --margin N           Padding in pixels (default 12)\n"
    "  --width N --height N Max image size for auto scale\n"
    "  --grid               Draw a light grid\n"
    "  --ppm                Write PPM instead of PNG\n"
    "  --bg RRGGBB          Background colour (default FFFFFF)\n"
    "  --tex / --no-tex     Typeset $...$ LaTeX in TY text (default on)\n"
    "\n"
    "Environment (booleans: 0/false/no/off = off, anything else = on):\n"
    "  FIDO_STDLIB_DIR      Directory with FCDstdlib_en.fcl etc.\n"
    "  FIDO_LIB_DIR         Extra .fcl directory (loaded after stdlib)\n"
    "  FIDO_FONT_DIR        Directory with the Liberation TTFs\n"
    "  FIDO_NO_STDLIB=1     Same as --no-stdlib\n"
    "  FIDO_NO_TEX=1        Same as --no-tex\n"
    "  FIDO_SCALE, FIDO_MARGIN, FIDO_JSON=1\n"
    "\n"
    "Config file (key = value, # comments):\n"
    "  stdlib, lib_dir, extensions, scale, margin, width, height, grid, ppm, bg, verbose, tex,\n"
    "  json, out\n";

typedef struct {
    const char *path;
    const char *prefix;
} LibArg;

typedef struct {
    char cmd[32];
    const char *file;
    const char *out;
    const char *config_path;
    const char *argv0;
    int stdlib;
    int extensions;
    int json;
    int quiet;
    int verbose;
    int scale, margin, max_w, max_h, grid, ppm, tex;
    uint32_t bg;
    char prefix[128];
    LibArg libs[64];
    int nlibs;
    const char *libdirs[16];
    int nlibdirs;
} Opt;

static int is_false(const char *v) {
    return fc_streq(v, "0") || fc_streq(v, "false") || fc_streq(v, "no") || fc_streq(v, "off") ||
           fc_streq(v, "");
}

/* -1 when unset, else 0/1 */
static int env_bool(const char *name) {
    const char *v = getenv(name);
    if (!v) return -1;
    return !is_false(v);
}

static void opt_init(Opt *o) {
    memset(o, 0, sizeof *o);
    o->stdlib = 1;
    o->extensions = 1;
    o->margin = 12;
    o->max_w = 1200;
    o->max_h = 900;
    o->bg = 0xFFFFFF;
    o->tex = 1;
    if (env_bool("FIDO_NO_STDLIB") == 1) o->stdlib = 0;
    if (env_bool("FIDO_NO_TEX") == 1) o->tex = 0;
    if (env_bool("FIDO_JSON") == 1) o->json = 1;
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
    if (fc_streq(k, "stdlib")) o->stdlib = !is_false(v);
    else if (fc_streq(k, "extensions"))
        o->extensions = !is_false(v);
    else if (fc_streq(k, "verbose"))
        o->verbose = !is_false(v);
    else if (fc_streq(k, "json"))
        o->json = !is_false(v);
    else if (fc_streq(k, "grid"))
        o->grid = !is_false(v);
    else if (fc_streq(k, "ppm"))
        o->ppm = !is_false(v);
    else if (fc_streq(k, "tex"))
        o->tex = !is_false(v);
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
    o->libs[o->nlibs].path = path;
    o->libs[o->nlibs].prefix = o->prefix[0] ? strdup(o->prefix) : NULL;
    o->nlibs++;
    o->prefix[0] = 0;
}

static int load_all_libs(Opt *o, FcLib *lib) {
    int i;
    int ok = 1;
    if (o->stdlib) {
        if (fc_lib_load_stdlib(lib, o->argv0) != FC_OK && !o->quiet)
            fprintf(stderr, "fcai: warning: standard libraries not found\n");
    }
    {
        const char *extra = getenv("FIDO_LIB_DIR");
        if (extra) fc_lib_load_dir(lib, extra);
    }
    for (i = 0; i < o->nlibdirs; i++) {
        if (fc_lib_load_dir(lib, o->libdirs[i]) != FC_OK && !o->quiet)
            fprintf(stderr, "fcai: warning: cannot load lib dir %s\n", o->libdirs[i]);
    }
    for (i = 0; i < o->nlibs; i++) {
        const char *pref = o->libs[i].prefix;
        if (fc_lib_load_file(lib, o->libs[i].path, pref) != FC_OK) {
            fprintf(stderr, "fcai: cannot load library %s\n", o->libs[i].path);
            ok = 0;
        }
    }
    return ok ? FC_OK : FC_ERR_IO;
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
                                    "FIDO_MARGIN",     "FIDO_JSON",    NULL};

static int cmd_config(const Opt *o, const FcLib *lib) {
    FcBuf b;
    int i;
    fc_buf_init(&b);
    fc_buf_printf(&b, "version = %s\n", FC_VERSION);
    fc_buf_printf(&b, "stdlib = %d\n", o->stdlib);
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
        fc_buf_printf(&b, "lib = %s%s%s\n", o->libs[i].path, o->libs[i].prefix ? " prefix=" : "",
                      o->libs[i].prefix ? o->libs[i].prefix : "");
    for (i = 0; k_env[i]; i++) {
        const char *v = getenv(k_env[i]);
        if (v) fc_buf_printf(&b, "# env %s = %s\n", k_env[i], v);
    }
    fc_buf_printf(&b, "macros_loaded = %d\n", fc_lib_count(lib));
    {
        int rc = write_out(o, b.p ? b.p : "", b.n);
        fc_buf_free(&b);
        return rc;
    }
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
    check_macros(d, lib);

    if (fc_streq(cmd, "context")) {
        s = o->json ? fc_to_json(d, lib) : fc_context(d, lib);
        rc = s ? write_out(o, s, fc_strlen(s)) : FC_ERR_NOMEM;
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

int main(int argc, char **argv) {
    Opt o;
    FcLib *lib;
    FcDrawing d;
    int i, rc;

    opt_init(&o);
    o.argv0 = argv[0];

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
        if (a[0] == '-' && a[1] != 0) {
            fprintf(stderr, "fcai: unknown option %s\n", a);
            return 1;
        }
        if (!o.cmd[0] &&
            (fc_streq(a, "context") || fc_streq(a, "describe") || fc_streq(a, "json") ||
             fc_streq(a, "dump") || fc_streq(a, "parse") || fc_streq(a, "emit") ||
             fc_streq(a, "validate") || fc_streq(a, "render") || fc_streq(a, "expand") ||
             fc_streq(a, "lib") || fc_streq(a, "config"))) {
            snprintf(o.cmd, sizeof o.cmd, "%s", a);
            continue;
        }
        if (!o.file) {
            o.file = a;
            continue;
        }
        fprintf(stderr, "fcai: extra argument %s\n", a);
        return 1;
    }

    lib = fc_lib_new();
    if (!lib) return 1;
    if (load_all_libs(&o, lib) != FC_OK) {
        fc_lib_free(lib);
        return 1;
    }

    if (fc_streq(o.cmd, "lib")) {
        rc = cmd_lib(&o, lib);
        fc_lib_free(lib);
        return rc == FC_OK ? 0 : 1;
    }
    if (fc_streq(o.cmd, "config")) {
        rc = cmd_config(&o, lib);
        fc_lib_free(lib);
        return rc == FC_OK ? 0 : 1;
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
