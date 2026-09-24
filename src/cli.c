#include "internal.h"

#include <dirent.h>
#include <errno.h>
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
    "  config     Show effective settings\n"
    "  diff A B   Primitive-level changes from A to B (--json for ops)\n"
    "  gui        Open the drawing editor (fcai-gui) for the user; Return sends\n"
    "             edits to the agent, Ctrl+Return ends the session\n"
    "  session    Talk to a running fcai-gui: wait|push|set|get|ack|quit DIR\n"
    "  skill      Print the fidocad agent skill; `skill install [DIR...]` installs it\n"
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
    "Environment:\n"
    "  FIDO_STDLIB_DIR      Directory with FCDstdlib_en.fcl etc.\n"
    "  FIDO_LIB_DIR         Extra .fcl directory (loaded after stdlib)\n"
    "  FIDO_USER_LIB_DIR    Your macro directory (default ~/Documents/FidoLib)\n"
    "  FIDO_NO_STDLIB=1     Same as --no-stdlib\n"
    "  FIDO_NO_TEX=1        Same as --no-tex\n"
    "  FIDO_SCALE, FIDO_MARGIN, FIDO_JSON\n"
    "\n"
    "Config file (key = value, # comments):\n"
    "  stdlib, lib_dir, extensions, scale, margin, width, height, grid, ppm, bg, verbose, tex\n";

typedef struct {
    const char *path;
    const char *prefix;
} LibArg;

typedef struct {
    char cmd[32];
    const char *file;
    const char *file2;
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

static void opt_init(Opt *o) {
    memset(o, 0, sizeof *o);
    o->stdlib = 1;
    o->extensions = 1;
    o->margin = 12;
    o->max_w = 1200;
    o->max_h = 900;
    o->bg = 0xFFFFFF;
    o->tex = 1;
    if (getenv("FIDO_NO_STDLIB")) o->stdlib = 0;
    if (getenv("FIDO_NO_TEX")) o->tex = 0;
    if (getenv("FIDO_JSON")) o->json = 1;
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
    if (fc_streq(k, "stdlib")) o->stdlib = !(fc_streq(v, "0") || fc_streq(v, "false"));
    else if (fc_streq(k, "extensions"))
        o->extensions = !(fc_streq(v, "0") || fc_streq(v, "false"));
    else if (fc_streq(k, "verbose"))
        o->verbose = !(fc_streq(v, "0") || fc_streq(v, "false"));
    else if (fc_streq(k, "json"))
        o->json = !(fc_streq(v, "0") || fc_streq(v, "false"));
    else if (fc_streq(k, "grid"))
        o->grid = !(fc_streq(v, "0") || fc_streq(v, "false"));
    else if (fc_streq(k, "ppm"))
        o->ppm = !(fc_streq(v, "0") || fc_streq(v, "false"));
    else if (fc_streq(k, "tex"))
        o->tex = !(fc_streq(v, "0") || fc_streq(v, "false"));
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
    /* ~/Documents/FidoLib: the user's own parts (silent when absent). */
    fc_lib_load_user_dir(lib);
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

static void mark_unresolved(FcDrawing *d, const FcLib *lib) {
    int i;
    for (i = 0; i < d->nprims; i++) {
        FcPrim *p = &d->prims[i];
        if (p->kind == FC_MACRO && !fc_lib_find(lib, p->u.macro.key)) {
            p->unresolved = 1;
            fc_drawing_diag(d, p->src_line, FC_DIAG_WARN, "unresolved macro '%s'",
                            p->u.macro.key ? p->u.macro.key : "");
        }
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

static int cmd_config(const Opt *o) {
    FcBuf b;
    fc_buf_init(&b);
    fc_buf_printf(&b, "version = %s\n", FC_VERSION);
    fc_buf_printf(&b, "stdlib = %d\n", o->stdlib);
    {
        char ulib[1024];
        if (fc_user_lib_dir(ulib, sizeof ulib)) fc_buf_printf(&b, "user_lib = %s\n", ulib);
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
    mark_unresolved(d, lib);

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

static int session_wait(const char *dir, int timeout_s) {
    char epath[4096], ppath[4096];
    long pos = 0;
    double waited = 0;
    FILE *pf;
    snprintf(epath, sizeof epath, "%s/events.jsonl", dir);
    snprintf(ppath, sizeof ppath, "%s/.events.pos", dir);
    pf = fopen(ppath, "rb");
    if (pf) {
        if (fscanf(pf, "%ld", &pos) != 1) pos = 0;
        fclose(pf);
    }
    for (;;) {
        FILE *f = fopen(epath, "rb");
        if (f) {
            if (fseek(f, pos, SEEK_SET) == 0) {
                char buf[131072];
                size_t n = fread(buf, 1, sizeof buf - 1, f);
                fclose(f);
                if (n > 0) {
                    char *nl;
                    buf[n] = 0;
                    nl = memchr(buf, '\n', n);
                    if (nl) {
                        size_t len = (size_t)(nl - buf) + 1;
                        fwrite(buf, 1, len, stdout);
                        fflush(stdout);
                        pos += (long)len;
                        pf = fopen(ppath, "wb");
                        if (pf) {
                            fprintf(pf, "%ld", pos);
                            fclose(pf);
                        }
                        return 0;
                    }
                }
            } else {
                fclose(f);
            }
        }
        if (timeout_s > 0 && waited >= timeout_s) {
            fprintf(stderr, "fcai: session wait: timeout after %d s\n", timeout_s);
            return 3;
        }
        {
            struct timespec ts = {0, 100000000};
            nanosleep(&ts, NULL);
        }
        waited += 0.1;
    }
}

static void session_usage(void) {
    fputs("usage: fcai session <command> DIR [args]\n"
          "  wait DIR [--timeout SECS]   print the next event line from the GUI\n"
          "  push DIR [FILE]             replace the drawing (FILE default stdin)\n"
          "  set DIR KEY VALUE           change a gui.conf setting live\n"
          "  get DIR                     append a full state event (then wait)\n"
          "  ack DIR [TEXT]              acknowledge the user's send (shown in the GUI)\n"
          "  quit DIR                    ask the GUI to close (Done)\n"
          "  path FILE                   print the default session dir for a drawing\n",
          stderr);
}

static int cmd_session(int argc, char **argv) {
    const char *sub = argc > 1 ? argv[1] : NULL;
    const char *dir;
    FcBuf b;
    int rc;
    if (!sub || fc_streq(sub, "-h") || fc_streq(sub, "--help") || fc_streq(sub, "help")) {
        session_usage();
        return sub ? 0 : 1;
    }
    if (fc_streq(sub, "path")) {
        char out[4096];
        if (argc < 3) {
            session_usage();
            return 1;
        }
        default_session_dir(argv[2], out, sizeof out);
        printf("%s\n", out);
        return 0;
    }
    dir = argc > 2 ? argv[2] : NULL;
    if (!dir) {
        session_usage();
        return 1;
    }
    if (fc_streq(sub, "wait")) {
        int timeout = 0, i;
        for (i = 3; i + 1 < argc; i++)
            if (fc_streq(argv[i], "--timeout")) fc_parse_int(argv[i + 1], &timeout);
        return session_wait(dir, timeout);
    }
    if (fc_streq(sub, "push")) {
        const char *file = argc > 3 ? argv[3] : "-";
        char *text = NULL;
        size_t len = 0;
        if (fc_read_file(file, &text, &len) != FC_OK) {
            fprintf(stderr, "fcai: cannot read %s\n", file);
            return 1;
        }
        fc_buf_init(&b);
        fc_buf_puts(&b, "{\"cmd\":\"push\",\"fcd\":");
        fc_json_escape(&b, text ? text : "");
        fc_buf_puts(&b, "}\n");
        rc = session_append(dir, &b);
        fc_buf_free(&b);
        free(text);
        return rc == FC_OK ? 0 : 1;
    }
    if (fc_streq(sub, "set")) {
        if (argc < 5) {
            session_usage();
            return 1;
        }
        fc_buf_init(&b);
        fc_buf_puts(&b, "{\"cmd\":\"set\",\"key\":");
        fc_json_escape(&b, argv[3]);
        fc_buf_puts(&b, ",\"value\":");
        fc_json_escape(&b, argv[4]);
        fc_buf_puts(&b, "}\n");
        rc = session_append(dir, &b);
        fc_buf_free(&b);
        return rc == FC_OK ? 0 : 1;
    }
    if (fc_streq(sub, "ack")) {
        const char *text = argc > 3 ? argv[3] : "";
        fc_buf_init(&b);
        fc_buf_puts(&b, "{\"cmd\":\"ack\",\"text\":");
        fc_json_escape(&b, text);
        fc_buf_puts(&b, "}\n");
        rc = session_append(dir, &b);
        fc_buf_free(&b);
        return rc == FC_OK ? 0 : 1;
    }
    if (fc_streq(sub, "get") || fc_streq(sub, "quit")) {
        char line[64];
        snprintf(line, sizeof line, "{\"cmd\":\"%s\"}\n", sub);
        fc_buf_init(&b);
        fc_buf_puts(&b, line);
        rc = session_append(dir, &b);
        fc_buf_free(&b);
        return rc == FC_OK ? 0 : 1;
    }
    fprintf(stderr, "fcai: unknown session command '%s'\n", sub);
    session_usage();
    return 1;
}

/* ---- fcai skill: the fidocad agent skill ------------------------------- */

static int find_skill_file(const char *argv0, char *out, size_t n) {
    char dir[4096];
    const char *slash = strrchr(argv0, '/');
    const char *env = getenv("FIDO_SKILL_DIR");
    size_t dlen = slash ? (size_t)(slash - argv0) : 0;
    int i;
    const char *fmts[] = {"%s/../share/skills/fidocad/SKILL.md",
                          "%s/share/skills/fidocad/SKILL.md",
                          "%s/../share/fidocad/skills/fidocad/SKILL.md"};
    if (dlen >= sizeof dir) dlen = sizeof dir - 1;
    memcpy(dir, argv0, dlen);
    dir[dlen] = 0;
    if (env && *env) {
        snprintf(out, n, "%s/fidocad/SKILL.md", env);
        if (file_exists(out)) return 1;
    }
    for (i = 0; i < 3; i++) {
        snprintf(out, n, fmts[i], dir);
        if (file_exists(out)) return 1;
    }
    snprintf(out, n, "%s", "/usr/local/share/fidocad/skills/fidocad/SKILL.md");
    if (file_exists(out)) return 1;
    snprintf(out, n, "%s", "/usr/share/fidocad/skills/fidocad/SKILL.md");
    if (file_exists(out)) return 1;
    snprintf(out, n, "%s", "share/skills/fidocad/SKILL.md");
    if (file_exists(out)) return 1;
    return 0;
}

/* Canonical source for `fcai skill install`: the Omarchy default-agents
   channel when the skill is installed there ($FIDO_SKILL_DIR overrides),
   otherwise the skill directory next to the binary. Agent directories get
   symlinks to this directory — like the Omarchy default skills — so every
   agent sees one skill and pi deduplicates the entries by real path. */
static int find_skill_install_dir(const char *argv0, char *out, size_t n) {
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
    if (!find_skill_file(argv0, file, sizeof file)) return 0;
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
    {NULL, NULL, 0},
};

static int skill_install(const char *srcdir, int argc, char **argv) {
    char targets[16][4096];
    const char *dirs[16];
    const char *home = getenv("HOME");
    int nd = 0, i, rc = 0;

    if (argc > 0) {
        for (i = 0; i < argc && nd < 16; i++) dirs[nd++] = argv[i];
    } else {
        if (!home || !*home) {
            fprintf(stderr, "fcai: HOME is not set; pass target directories\n");
            return 1;
        }
        for (i = 0; k_skill_agents[i].parent && nd < 16; i++) {
            char parent[3072];
            struct stat st;
            size_t plen, slen;
            snprintf(parent, sizeof parent, "%s/%s", home, k_skill_agents[i].parent);
            if (!k_skill_agents[i].always && stat(parent, &st) != 0) continue;
            plen = fc_strlen(parent);
            slen = fc_strlen(k_skill_agents[i].skills);
            if (plen + 1 + slen >= sizeof targets[nd]) continue;
            memcpy(targets[nd], parent, plen);
            targets[nd][plen] = '/';
            memcpy(targets[nd] + plen + 1, k_skill_agents[i].skills, slen + 1);
            dirs[nd] = targets[nd];
            nd++;
        }
    }

    for (i = 0; i < nd; i++) {
        char dst[4096];
        struct stat st;
        snprintf(dst, sizeof dst, "%s/fidocad", dirs[i]);
        if (!mkdir_p(dirs[i])) {
            fprintf(stderr, "fcai: cannot create %s\n", dirs[i]);
            rc = 1;
            continue;
        }
        if (lstat(dst, &st) == 0) {
            if (S_ISLNK(st.st_mode)) {
                unlink(dst); /* stale or dangling link: replace it */
            } else if (S_ISDIR(st.st_mode)) {
                char skill[4096];
                size_t dl = fc_strlen(dst);
                if (dl + 10 > sizeof skill) { rc = 1; continue; }
                memcpy(skill, dst, dl);
                memcpy(skill + dl, "/SKILL.md", 10);
                if (!file_exists(skill)) {
                    fprintf(stderr, "fcai: %s is not a fidocad skill; leaving it\n", dst);
                    rc = 1;
                    continue;
                }
                rm_rf(dst); /* a copy made by an older fcai: replace with the link */
            } else {
                fprintf(stderr, "fcai: %s exists and is not a directory; leaving it\n", dst);
                rc = 1;
                continue;
            }
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

static int cmd_skill(const char *argv0, int argc, char **argv) {
    char path[4096];
    const char *sub = argc > 0 ? argv[0] : NULL;
    if (!find_skill_file(argv0, path, sizeof path)) {
        fprintf(stderr, "fcai: fidocad skill not found (set FIDO_SKILL_DIR)\n");
        return 1;
    }
    if (!sub || fc_streq(sub, "show") || fc_streq(sub, "print")) {
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
    if (fc_streq(sub, "install")) {
        char dir[4096];
        if (!find_skill_install_dir(argv0, dir, sizeof dir)) {
            fprintf(stderr, "fcai: fidocad skill not found (set FIDO_SKILL_DIR)\n");
            return 1;
        }
        make_abs(dir, sizeof dir);
        return skill_install(dir, argc - 1, argv + 1);
    }
    if (fc_streq(sub, "-h") || fc_streq(sub, "--help") || fc_streq(sub, "help")) {
        fputs("usage: fcai skill [show|path]\n"
              "       fcai skill install [DIR...]   symlinks the skill into each agent's\n"
              "                                     skills dir (default: ~/.agents/skills,\n"
              "                                     ~/.pi, ~/.claude, ~/.codex, ...)\n",
              stdout);
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
    if (argc > 1 && fc_streq(argv[1], "skill")) return cmd_skill(argv[0], argc - 2, argv + 2);

    opt_init(&o);
    o.argv0 = argv[0];

    /* parse argv: flags anywhere, first non-flag is command if it matches, else file */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (fc_streq(a, "-h") || fc_streq(a, "--help") || fc_streq(a, "help")) {
            fputs(k_help, stdout);
            return 0;
        }
        if (fc_streq(a, "--version") || fc_streq(a, "version")) {
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
             fc_streq(a, "lib") || fc_streq(a, "config") || fc_streq(a, "diff"))) {
            snprintf(o.cmd, sizeof o.cmd, "%s", a);
            continue;
        }
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
    load_all_libs(&o, lib);

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
        rc = cmd_config(&o);
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
