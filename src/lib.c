#include "internal.h"

#include <dirent.h>
#include <sys/stat.h>

static void slot_insert(int *slot, int nslot, const char *key, int idx) {
    int j = (int)(fc_hash(key) & (uint32_t)(nslot - 1));
    while (slot[j] >= 0) j = (j + 1) & (nslot - 1);
    slot[j] = idx;
}

/* Grows the table so it stays at most half full. Returns 0 on success. */
static int lib_reserve_slots(FcLib *lib, int n) {
    int i, nslot;
    int *slot;
    if (lib->nslot >= 2 * n + 8) return 0;
    nslot = lib->nslot ? lib->nslot : 8;
    while (nslot < 2 * n + 8) nslot *= 2;
    slot = (int *)malloc((size_t)nslot * sizeof(int));
    if (!slot) return -1;
    for (i = 0; i < nslot; i++) slot[i] = -1;
    for (i = 0; i < lib->n; i++) slot_insert(slot, nslot, lib->macros[i].key, i);
    free(lib->slot);
    lib->slot = slot;
    lib->nslot = nslot;
    return 0;
}

static int lib_find_index(const FcLib *lib, const char *key) {
    uint32_t h;
    int j, n;
    if (!lib || !lib->nslot || !key) return -1;
    h = fc_hash(key);
    j = (int)(h & (uint32_t)(lib->nslot - 1));
    n = lib->nslot;
    while (n--) {
        int idx = lib->slot[j];
        if (idx < 0) return -1;
        if (fc_streq(lib->macros[idx].key, key)) return idx;
        j = (j + 1) & (lib->nslot - 1);
    }
    return -1;
}

FcLib *fc_lib_new(void) {
    FcLib *lib = (FcLib *)calloc(1, sizeof *lib);
    if (!lib) return NULL;
    fc_arena_init(&lib->arena, 1 << 16);
    return lib;
}

void fc_lib_free(FcLib *lib) {
    if (!lib) return;
    free(lib->macros);
    free(lib->slot);
    fc_arena_free(&lib->arena);
    free(lib);
}

const FcMacro *fc_lib_find(const FcLib *lib, const char *key) {
    int i = lib_find_index(lib, key);
    return i >= 0 ? &lib->macros[i] : NULL;
}

int fc_lib_count(const FcLib *lib) { return lib ? lib->n : 0; }

const FcMacro *fc_lib_at(const FcLib *lib, int i) {
    if (!lib || i < 0 || i >= lib->n) return NULL;
    return &lib->macros[i];
}

static FcMacro *lib_upsert(FcLib *lib, const char *key) {
    int idx = lib_find_index(lib, key);
    FcMacro *m;
    const char *k;
    if (idx >= 0) return &lib->macros[idx];
    if (lib_reserve_slots(lib, lib->n + 1) != 0) return NULL;
    k = fc_arena_strdup(&lib->arena, key);
    if (!k) return NULL;
    if (lib->n >= lib->cap) {
        int ncap = FC_GROW(lib->cap);
        FcMacro *nm = (FcMacro *)realloc(lib->macros, (size_t)ncap * sizeof(FcMacro));
        if (!nm) return NULL;
        lib->macros = nm;
        lib->cap = ncap;
    }
    m = &lib->macros[lib->n];
    memset(m, 0, sizeof *m);
    m->key = k;
    slot_insert(lib->slot, lib->nslot, k, lib->n);
    lib->n++;
    return m;
}

static int is_blankish(const char *s) {
    size_t n = fc_strlen(s);
    return n <= 1;
}

int fc_lib_load_text(FcLib *lib, const char *text, size_t len, const char *prefix) {
    const char *p, *end;
    char *line = NULL;
    size_t line_cap = 0;
    char category[128] = "";
    char library[128] = "";
    FcMacro *cur = NULL;
    FcBuf body;

    if (!lib || !text) return FC_ERR_ARG;
    if (len == 0) len = fc_strlen(text);
    p = text;
    end = text + len;
    fc_buf_init(&body);

    while (p < end) {
        const char *start = p;
        size_t n;
        char *s;
        while (p < end && *p != '\n' && *p != '\r') p++;
        n = (size_t)(p - start);
        if (n + 1 > line_cap) {
            size_t ncap = n + 8 < 256 ? 256 : n + 8;
            char *nl = (char *)realloc(line, ncap);
            if (!nl) {
                free(line);
                fc_buf_free(&body);
                return FC_ERR_NOMEM;
            }
            line = nl;
            line_cap = ncap;
        }
        memcpy(line, start, n);
        line[n] = 0;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;

        /* trim */
        {
            char *a = line, *z;
            while (*a == ' ' || *a == '\t') a++;
            z = a + fc_strlen(a);
            while (z > a && (z[-1] == ' ' || z[-1] == '\t')) *--z = 0;
            s = a;
        }
        if (is_blankish(s)) continue;
        if (s[0] == ']' && s[1] == 0) continue;

        if (s[0] == '{') {
            char *q = s + 1;
            char *e = strchr(q, '}');
            size_t ln;
            if (e) *e = 0;
            while (*q == ' ') q++;
            ln = fc_strlen(q);
            if (ln >= sizeof category) ln = sizeof category - 1;
            memcpy(category, q, ln);
            category[ln] = 0;
            continue;
        }

        if (s[0] == '[') {
            char key[128];
            char longn[256];
            const char *q = s + 1;
            size_t ki = 0;
            while (*q && *q != ' ' && *q != ']') {
                if (ki + 1 < sizeof key) key[ki++] = *q;
                q++;
            }
            key[ki] = 0;
            while (*q == ' ') q++;
            {
                const char *e = strchr(q, ']');
                size_t ln = e ? (size_t)(e - q) : fc_strlen(q);
                while (ln && q[ln - 1] == ' ') ln--;
                if (ln >= sizeof longn) ln = sizeof longn - 1;
                memcpy(longn, q, ln);
                longn[ln] = 0;
            }
            if (fc_streq(key, "FIDOLIB")) {
                size_t ln = fc_strlen(longn);
                if (ln >= sizeof library) ln = sizeof library - 1;
                memcpy(library, longn, ln);
                library[ln] = 0;
                cur = NULL;
                continue;
            }
            if (key[0] == 0) {
                cur = NULL;
                continue;
            }
            {
                char full[256];
                if (prefix && prefix[0])
                    snprintf(full, sizeof full, "%s.%s", prefix, key);
                else
                    snprintf(full, sizeof full, "%s", key);
                fc_tolower_buf(full);
                if (cur && body.n) {
                    cur->body = fc_arena_strdup(&lib->arena, body.p ? body.p : "");
                }
                fc_buf_free(&body);
                fc_buf_init(&body);
                cur = lib_upsert(lib, full);
                if (cur) {
                    cur->long_name = fc_arena_strdup(&lib->arena, longn);
                    cur->category = fc_arena_strdup(&lib->arena, category);
                    cur->lib_name = fc_arena_strdup(&lib->arena, library);
                    cur->prefix = fc_arena_strdup(&lib->arena, prefix ? prefix : "");
                    cur->body = "";
                }
            }
            continue;
        }

        if (cur) {
            if (body.n) fc_buf_putc(&body, '\n');
            fc_buf_puts(&body, s);
        }
    }
    if (cur && body.n) cur->body = fc_arena_strdup(&lib->arena, body.p);
    fc_buf_free(&body);
    free(line);
    return FC_OK;
}

const char *fc_lib_prefix_for_file(const char *filename) {
    /* used as default when loading a directory; stdlib special-cased in load_stdlib */
    static char buf[128];
    const char *base, *dot, *slash;
    size_t n;
    if (!filename) return "";
    slash = strrchr(filename, '/');
    base = slash ? slash + 1 : filename;
    if (!strncmp(base, "FCDstdlib", 9)) return "";
    dot = strrchr(base, '.');
    n = dot ? (size_t)(dot - base) : fc_strlen(base);
    /* strip _en */
    if (n > 3 && base[n - 3] == '_' && base[n - 2] == 'e' && base[n - 1] == 'n') n -= 3;
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, base, n);
    buf[n] = 0;
    fc_tolower_buf(buf);
    return buf;
}

int fc_lib_load_file(FcLib *lib, const char *path, const char *prefix) {
    char *text = NULL;
    size_t len = 0;
    int rc = fc_read_file(path, &text, &len);
    if (rc != FC_OK) return rc;
    if (!prefix) prefix = fc_lib_prefix_for_file(path);
    rc = fc_lib_load_text(lib, text, len, prefix);
    free(text);
    return rc;
}

/* Loads every *.fcl in dir, except names that also exist in shadow (if given). */
static int load_dir(FcLib *lib, const char *dir, const char *shadow) {
    DIR *dp;
    struct dirent *de;
    struct stat st;
    int n = 0;
    if (!dir) return FC_ERR_ARG;
    dp = opendir(dir);
    if (!dp) return FC_ERR_IO;
    while ((de = readdir(dp))) {
        size_t nlen = fc_strlen(de->d_name);
        char path[1024];
        if (nlen < 5 || strcmp(de->d_name + nlen - 4, ".fcl") != 0) continue;
        if (shadow) {
            snprintf(path, sizeof path, "%s/%s", shadow, de->d_name);
            if (stat(path, &st) == 0) continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        if (fc_lib_load_file(lib, path, fc_lib_prefix_for_file(de->d_name)) == FC_OK) n++;
    }
    closedir(dp);
    return n > 0 ? FC_OK : FC_ERR_IO;
}

int fc_lib_load_dir(FcLib *lib, const char *dir) { return load_dir(lib, dir, NULL); }

/* ---- user library directory ------------------------------------------- */

static char *trim_ws(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + fc_strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

/* $XDG_DOCUMENTS_DIR from ~/.config/user-dirs.dirs, if that file exists. */
static int xdg_documents_dir(char *out, size_t cap) {
    const char *home = getenv("HOME");
    const char *cfg = getenv("XDG_CONFIG_HOME");
    char path[1024];
    char *text = NULL;
    size_t len = 0;
    char *p;
    int found = 0;
    if (!home || !*home) return 0;
    if (cfg && *cfg) snprintf(path, sizeof path, "%s/user-dirs.dirs", cfg);
    else snprintf(path, sizeof path, "%s/.config/user-dirs.dirs", home);
    if (fc_read_file(path, &text, &len) != FC_OK) return 0;
    for (p = text; p && *p;) {
        char *nl = strchr(p, '\n');
        char *eq;
        if (nl) *nl = 0;
        eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            if (fc_streq(trim_ws(p), "XDG_DOCUMENTS_DIR")) {
                const char *v = trim_ws(eq + 1);
                char quote = (v[0] == '"' || v[0] == '\'') ? v[0] : 0;
                size_t o = 0;
                if (quote) v++;
                while (*v && o + 1 < cap) {
                    if (quote && *v == quote && v[1] == 0) break;
                    if (strncmp(v, "$HOME", 5) == 0 && (v[5] == '/' || v[5] == 0)) {
                        int m = snprintf(out + o, cap - o, "%s", home);
                        if (m < 0 || (size_t)m >= cap - o) { o = cap - 1; break; }
                        o += (size_t)m;
                        v += 5;
                        continue;
                    }
                    out[o++] = *v++;
                }
                out[o] = 0;
                found = o > 0;
                break;
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    free(text);
    return found;
}

static int legacy_user_lib_dir(char *out, size_t cap) {
    const char *home = getenv("HOME");
    const char *env;
    char doc[1024];
    size_t n;
    if (!out || cap == 0) return 0;
    env = getenv("XDG_DOCUMENTS_DIR");
    if (env && *env) snprintf(doc, sizeof doc, "%s", env);
    else if (!xdg_documents_dir(doc, sizeof doc)) {
        if (!home || !*home) return 0;
        snprintf(doc, sizeof doc, "%s/Documents", home);
    }
    n = fc_strlen(doc);
    while (n > 1 && doc[n - 1] == '/') doc[--n] = 0;
    snprintf(out, cap, "%s/FidoLib", doc);
    return 1;
}

int fc_user_lib_dir(char *out, size_t cap) {
    const char *env = getenv("FIDO_USER_LIB_DIR");
    char root[4096];
    int n;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (env && *env) n = snprintf(out, cap, "%s", env);
    else if (fc_app_dir(root, sizeof root)) n = snprintf(out, cap, "%s/lib", root);
    else return 0;
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return 0; }
    return 1;
}

/* Silent no-op when the directory is absent. */
static int load_user_dir(FcLib *lib) {
    const char *override = getenv("FIDO_USER_LIB_DIR");
    const char *root = getenv("FCAI_HOME");
    char dir[4096], legacy[1024];
    int old_rc = FC_ERR_IO, rc;
    if (!lib || !fc_user_lib_dir(dir, sizeof dir)) return FC_ERR_ARG;
    /* Transitional compatibility: explicit overrides isolate the library;
       otherwise old Documents libraries load first, so new definitions win,
       and a file in the user directory replaces the old file of that name. */
    if ((!override || !*override) && (!root || !*root) &&
        legacy_user_lib_dir(legacy, sizeof legacy) && !fc_streq(legacy, dir))
        old_rc = load_dir(lib, legacy, dir);
    rc = fc_lib_load_dir(lib, dir);
    return rc == FC_OK || old_rc == FC_OK ? FC_OK : rc;
}

static int try_stdlib_dir(FcLib *lib, const char *dir) {
    struct stat st;
    char path[1024];
    int ok = 0;
    static const struct {
        const char *file;
        const char *prefix;
    } files[] = {{"FCDstdlib_en.fcl", ""},
                 {"elettrotecnica_en.fcl", "elettrotecnica"},
                 {"EY_Libraries.fcl", "EY_Libraries"},
                 {"IHRAM_en.fcl", "IHRAM"},
                 {"PCB_en.fcl", "PCB"},
                 {NULL, NULL}};
    int i;
    if (!dir || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    for (i = 0; files[i].file; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i].file);
        if (stat(path, &st) == 0) {
            if (fc_lib_load_file(lib, path, files[i].prefix) == FC_OK) ok = 1;
        }
    }
    return ok;
}

static int try_stdlib_cb(void *ctx, const char *dir) { return try_stdlib_dir((FcLib *)ctx, dir); }

int fc_lib_load_stdlib(FcLib *lib) {
    const char *env = getenv("FIDO_STDLIB_DIR");
    if (env && try_stdlib_dir(lib, env)) return FC_OK;
    return fc_find_data("lib", try_stdlib_cb, lib) ? FC_OK : FC_ERR_IO;
}

int fc_lib_load_all(FcLib *lib, int stdlib, const char *const *dirs, int ndirs,
                    const char *const *files, const char *const *prefixes, int nfiles,
                    FcLibReport report, void *ctx) {
    const char *extra = getenv("FIDO_LIB_DIR");
    char msg[1100];
    int i, rc = FC_OK;
    if (stdlib && fc_lib_load_stdlib(lib) != FC_OK && report)
        report(ctx, 0, "standard libraries not found");
    load_user_dir(lib);
    if (extra && *extra) fc_lib_load_dir(lib, extra);
    for (i = 0; i < ndirs; i++) {
        if (fc_lib_load_dir(lib, dirs[i]) == FC_OK || !report) continue;
        snprintf(msg, sizeof msg, "cannot load lib dir %s", dirs[i]);
        report(ctx, 0, msg);
    }
    for (i = 0; i < nfiles; i++) {
        if (fc_lib_load_file(lib, files[i], prefixes ? prefixes[i] : NULL) == FC_OK) continue;
        rc = FC_ERR_IO;
        if (!report) continue;
        snprintf(msg, sizeof msg, "cannot load library %s", files[i]);
        report(ctx, 1, msg);
    }
    return rc;
}

/* Appends a deep copy of src (strings and point arrays go into dst's arena). */
static FcPrim *copy_prim(FcDrawing *dst, const FcPrim *src) {
    FcPrim *p = fc_drawing_push(dst);
    if (!p) return NULL;
    *p = *src;
    if (src->kind == FC_POLY && src->u.poly.n > 0) {
        p->u.poly.pts =
            (FcPoint *)fc_arena_alloc(&dst->arena, (size_t)src->u.poly.n * sizeof(FcPoint));
        if (p->u.poly.pts)
            memcpy(p->u.poly.pts, src->u.poly.pts, (size_t)src->u.poly.n * sizeof(FcPoint));
        else
            p->u.poly.n = 0;
    } else if (src->kind == FC_CURVE && src->u.curve.n > 0) {
        p->u.curve.pts =
            (FcPoint *)fc_arena_alloc(&dst->arena, (size_t)src->u.curve.n * sizeof(FcPoint));
        if (p->u.curve.pts)
            memcpy(p->u.curve.pts, src->u.curve.pts, (size_t)src->u.curve.n * sizeof(FcPoint));
        else
            p->u.curve.n = 0;
    }
    if (src->kind == FC_MACRO && src->u.macro.key)
        p->u.macro.key = fc_arena_strdup(&dst->arena, src->u.macro.key);
    if (src->kind == FC_TEXT) {
        if (src->u.text.font) p->u.text.font = fc_arena_strdup(&dst->arena, src->u.text.font);
        if (src->u.text.text) p->u.text.text = fc_arena_strdup(&dst->arena, src->u.text.text);
    }
    if (src->has_name) {
        if (src->name.font) p->name.font = fc_arena_strdup(&dst->arena, src->name.font);
        if (src->name.text) p->name.text = fc_arena_strdup(&dst->arena, src->name.text);
    }
    if (src->has_value) {
        if (src->value.font) p->value.font = fc_arena_strdup(&dst->arena, src->value.font);
        if (src->value.text) p->value.text = fc_arena_strdup(&dst->arena, src->value.text);
    }
    return p;
}

enum { BODY_NONE = 0, BODY_BUILDING, BODY_DONE };

/* Each macro body is parsed and flattened once per fc_expand call, then stamped out
 * per instance. A body met again while it is still being built is a cycle. */
typedef struct {
    const FcLib *lib;
    FcDrawing *top;     /* diagnostics go here */
    FcDrawing *flat;    /* per macro index */
    unsigned char *state;
    unsigned char *warned; /* per macro index: diagnostic already given */
    int line;           /* source line of the top-level MC being expanded */
    long total;         /* primitives created so far, cached bodies included */
    int over_budget;
} ExpandCtx;

static int expand_into(ExpandCtx *cx, FcDrawing *dst, const FcDrawing *src, int depth);

static int budget_ok(ExpandCtx *cx) {
    if (!cx->over_budget && cx->total < FC_MAX_EXPANDED_PRIMS) return 1;
    if (!cx->over_budget)
        fc_drawing_diag(cx->top, cx->line, FC_DIAG_ERR, "macro expansion stopped at %d primitives",
                        FC_MAX_EXPANDED_PRIMS);
    cx->over_budget = 1;
    return 0;
}

static const FcDrawing *macro_body(ExpandCtx *cx, const FcMacro *m, int depth) {
    int idx = (int)(m - cx->lib->macros);
    FcDrawing body;
    if (cx->state[idx] == BODY_DONE) return &cx->flat[idx];
    if (cx->state[idx] == BODY_BUILDING || depth >= FC_MAX_MACRO_DEPTH) {
        if (!cx->warned[idx]) {
            if (cx->state[idx] == BODY_BUILDING)
                fc_drawing_diag(cx->top, cx->line, FC_DIAG_WARN,
                                "recursive macro '%s' not expanded", m->key);
            else
                fc_drawing_diag(cx->top, cx->line, FC_DIAG_WARN, "macro '%s' nested deeper than %d",
                                m->key, FC_MAX_MACRO_DEPTH);
        }
        cx->warned[idx] = 1;
        return NULL;
    }
    cx->state[idx] = BODY_BUILDING;
    fc_drawing_init(&body);
    fc_drawing_init(&cx->flat[idx]);
    fc_parse(&body, m->body, fc_strlen(m->body));
    expand_into(cx, &cx->flat[idx], &body, depth + 1);
    fc_drawing_free(&body);
    cx->state[idx] = BODY_DONE;
    return &cx->flat[idx];
}

/* copy_prim that counts against the expansion budget */
static FcPrim *emit_prim(ExpandCtx *cx, FcDrawing *dst, const FcPrim *src) {
    FcPrim *q = copy_prim(dst, src);
    if (q) cx->total++;
    return q;
}

static void push_label_text(ExpandCtx *cx, FcDrawing *dst, const FcLabel *l) {
    FcPrim t;
    fc_prim_init(&t);
    t.kind = FC_TEXT;
    t.layer = l->layer;
    t.u.text = *l;
    emit_prim(cx, dst, &t);
}

static int expand_into(ExpandCtx *cx, FcDrawing *dst, const FcDrawing *src, int depth) {
    int i, j;
    for (i = 0; i < src->nprims; i++) {
        const FcPrim *p = &src->prims[i];
        const FcDrawing *body = NULL;
        if (depth == 0) cx->line = p->src_line;
        if (!budget_ok(cx)) return FC_ERR_ARG;
        if (p->kind != FC_MACRO) {
            if (!emit_prim(cx, dst, p)) return FC_ERR_NOMEM;
            continue;
        }
        {
            const FcMacro *m = fc_lib_find(cx->lib, p->u.macro.key);
            if (m) body = macro_body(cx, m, depth);
        }
        if (!body) {
            FcPrim *q = emit_prim(cx, dst, p);
            if (!q) return FC_ERR_NOMEM;
            q->unresolved = 1;
            continue;
        }
        for (j = 0; j < body->nprims; j++) {
            FcPrim *q;
            if (!budget_ok(cx)) return FC_ERR_ARG;
            q = emit_prim(cx, dst, &body->prims[j]);
            if (!q) return FC_ERR_NOMEM;
            fc_xform_prim(q, p->u.macro.orient, p->u.macro.mirror, p->u.macro.p.x, p->u.macro.p.y);
        }
        /* name/value of the instance live in parent space — emit as text */
        if (p->has_name) push_label_text(cx, dst, &p->name);
        if (p->has_value) push_label_text(cx, dst, &p->value);
    }
    return FC_OK;
}

int fc_expand(const FcDrawing *src, FcDrawing *dst, const FcLib *lib) {
    ExpandCtx cx;
    int i, n = fc_lib_count(lib), rc;
    memset(&cx, 0, sizeof cx);
    cx.lib = lib;
    cx.top = dst;
    if (n > 0) {
        cx.flat = (FcDrawing *)calloc((size_t)n, sizeof *cx.flat);
        cx.state = (unsigned char *)calloc((size_t)n, 1);
        cx.warned = (unsigned char *)calloc((size_t)n, 1);
        if (!cx.flat || !cx.state || !cx.warned) {
            free(cx.flat);
            free(cx.state);
            free(cx.warned);
            return FC_ERR_NOMEM;
        }
    }
    rc = expand_into(&cx, dst, src, 0);
    if (cx.over_budget) rc = FC_ERR_ARG;
    dst->cfg = src->cfg;
    memcpy(dst->layers, src->layers, sizeof dst->layers);
    for (i = 0; i < n; i++)
        if (cx.state[i] != BODY_NONE) fc_drawing_free(&cx.flat[i]);
    free(cx.flat);
    free(cx.state);
    free(cx.warned);
    return rc;
}
