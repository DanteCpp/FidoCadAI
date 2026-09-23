#include "internal.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static void lib_rehash(FcLib *lib) {
    int i, nslot;
    int *slot;
    nslot = 8;
    while (nslot < lib->n * 2 + 8) nslot *= 2;
    slot = (int *)malloc((size_t)nslot * sizeof(int));
    if (!slot) return;
    for (i = 0; i < nslot; i++) slot[i] = -1;
    for (i = 0; i < lib->n; i++) {
        uint32_t h = fc_hash(lib->macros[i].key);
        int j = (int)(h & (uint32_t)(nslot - 1));
        while (slot[j] >= 0) j = (j + 1) & (nslot - 1);
        slot[j] = i;
    }
    free(lib->slot);
    lib->slot = slot;
    lib->nslot = nslot;
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
    if (idx >= 0) return &lib->macros[idx];
    if (lib->n >= lib->cap) {
        int ncap = FC_GROW(lib->cap);
        FcMacro *nm = (FcMacro *)realloc(lib->macros, (size_t)ncap * sizeof(FcMacro));
        if (!nm) return NULL;
        lib->macros = nm;
        lib->cap = ncap;
    }
    m = &lib->macros[lib->n++];
    memset(m, 0, sizeof *m);
    m->key = fc_arena_strdup(&lib->arena, key);
    lib_rehash(lib);
    /* pointer may have moved */
    idx = lib_find_index(lib, key);
    return idx >= 0 ? &lib->macros[idx] : NULL;
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

int fc_lib_load_dir(FcLib *lib, const char *dir) {
    DIR *dp;
    struct dirent *de;
    int n = 0;
    if (!dir) return FC_ERR_ARG;
    dp = opendir(dir);
    if (!dp) return FC_ERR_IO;
    while ((de = readdir(dp))) {
        size_t nlen = fc_strlen(de->d_name);
        char path[1024];
        if (nlen < 5) continue;
        if (strcmp(de->d_name + nlen - 4, ".fcl") != 0) continue;
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        if (fc_lib_load_file(lib, path, fc_lib_prefix_for_file(de->d_name)) == FC_OK) n++;
    }
    closedir(dp);
    return n > 0 ? FC_OK : FC_ERR_IO;
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

static void dirname_of(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, cap, ".");
        return;
    }
    if (slash == path) {
        snprintf(out, cap, "/");
        return;
    }
    {
        size_t n = (size_t)(slash - path);
        if (n >= cap) n = cap - 1;
        memcpy(out, path, n);
        out[n] = 0;
    }
}

int fc_lib_load_stdlib(FcLib *lib, const char *argv0) {
    char exe[1024];
    char dir[1024];
    char cand[1400];
    const char *env;
    ssize_t n;

    env = getenv("FIDO_STDLIB_DIR");
    if (env && try_stdlib_dir(lib, env)) return FC_OK;

    n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        dirname_of(exe, dir, sizeof dir);
        snprintf(cand, sizeof cand, "%s/../share/fidocad/lib", dir);
        if (try_stdlib_dir(lib, cand)) return FC_OK;
        snprintf(cand, sizeof cand, "%s/../share/lib", dir);
        if (try_stdlib_dir(lib, cand)) return FC_OK;
        snprintf(cand, sizeof cand, "%s/share/lib", dir);
        if (try_stdlib_dir(lib, cand)) return FC_OK;
    }
    if (argv0) {
        dirname_of(argv0, dir, sizeof dir);
        snprintf(cand, sizeof cand, "%s/share/lib", dir);
        if (try_stdlib_dir(lib, cand)) return FC_OK;
        snprintf(cand, sizeof cand, "%s/../share/lib", dir);
        if (try_stdlib_dir(lib, cand)) return FC_OK;
    }
    if (try_stdlib_dir(lib, "/usr/local/share/fidocad/lib")) return FC_OK;
    if (try_stdlib_dir(lib, "/usr/share/fidocad/lib")) return FC_OK;
    if (try_stdlib_dir(lib, "share/lib")) return FC_OK;
    return FC_ERR_IO;
}

static void copy_prim(FcDrawing *dst, const FcPrim *src) {
    FcPrim *p = fc_drawing_push(dst);
    int i;
    if (!p) return;
    *p = *src;
    if (src->kind == FC_POLY && src->u.poly.n > 0) {
        p->u.poly.pts =
            (FcPoint *)fc_arena_alloc(&dst->arena, (size_t)src->u.poly.n * sizeof(FcPoint));
        if (p->u.poly.pts)
            memcpy(p->u.poly.pts, src->u.poly.pts, (size_t)src->u.poly.n * sizeof(FcPoint));
    } else if (src->kind == FC_CURVE && src->u.curve.n > 0) {
        p->u.curve.pts =
            (FcPoint *)fc_arena_alloc(&dst->arena, (size_t)src->u.curve.n * sizeof(FcPoint));
        if (p->u.curve.pts)
            memcpy(p->u.curve.pts, src->u.curve.pts, (size_t)src->u.curve.n * sizeof(FcPoint));
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
    (void)i;
}

static int expand_into(FcDrawing *dst, const FcDrawing *src, const FcLib *lib, int depth);

static int expand_macro_body(FcDrawing *dst, const FcMacro *m, const FcLib *lib, int orient,
                             int mirror, double ox, double oy, int depth) {
    FcDrawing body, flat;
    int i;
    if (depth >= FC_MAX_MACRO_DEPTH) return FC_OK;
    fc_drawing_init(&body);
    fc_parse(&body, m->body, fc_strlen(m->body));
    fc_drawing_init(&flat);
    expand_into(&flat, &body, lib, depth + 1);
    for (i = 0; i < flat.nprims; i++) {
        FcPrim tmp = flat.prims[i];
        /* deep-copy points into dst arena via copy_prim after xform */
        if (tmp.kind == FC_POLY && tmp.u.poly.n) {
            FcPoint *pts =
                (FcPoint *)fc_arena_alloc(&dst->arena, (size_t)tmp.u.poly.n * sizeof(FcPoint));
            if (pts) memcpy(pts, tmp.u.poly.pts, (size_t)tmp.u.poly.n * sizeof(FcPoint));
            tmp.u.poly.pts = pts;
        }
        if (tmp.kind == FC_CURVE && tmp.u.curve.n) {
            FcPoint *pts =
                (FcPoint *)fc_arena_alloc(&dst->arena, (size_t)tmp.u.curve.n * sizeof(FcPoint));
            if (pts) memcpy(pts, tmp.u.curve.pts, (size_t)tmp.u.curve.n * sizeof(FcPoint));
            tmp.u.curve.pts = pts;
        }
        fc_xform_prim(&tmp, orient, mirror, ox, oy);
        copy_prim(dst, &tmp);
    }
    fc_drawing_free(&body);
    fc_drawing_free(&flat);
    return FC_OK;
}

static int expand_into(FcDrawing *dst, const FcDrawing *src, const FcLib *lib, int depth) {
    int i;
    for (i = 0; i < src->nprims; i++) {
        const FcPrim *p = &src->prims[i];
        if (p->kind == FC_MACRO) {
            const FcMacro *m = lib ? fc_lib_find(lib, p->u.macro.key) : NULL;
            if (!m) {
                FcPrim *q;
                copy_prim(dst, p);
                q = &dst->prims[dst->nprims - 1];
                q->unresolved = 1;
                continue;
            }
            expand_macro_body(dst, m, lib, p->u.macro.orient, p->u.macro.mirror, p->u.macro.p.x,
                              p->u.macro.p.y, depth);
            /* name/value of the instance live in parent space — emit as text */
            if (p->has_name) {
                FcPrim t;
                fc_prim_init(&t);
                t.kind = FC_TEXT;
                t.layer = p->name.layer;
                t.u.text = p->name;
                copy_prim(dst, &t);
            }
            if (p->has_value) {
                FcPrim t;
                fc_prim_init(&t);
                t.kind = FC_TEXT;
                t.layer = p->value.layer;
                t.u.text = p->value;
                copy_prim(dst, &t);
            }
        } else {
            copy_prim(dst, p);
        }
    }
    dst->cfg = src->cfg;
    memcpy(dst->layers, src->layers, sizeof dst->layers);
    return FC_OK;
}

int fc_expand(const FcDrawing *src, FcDrawing *dst, const FcLib *lib) {
    return expand_into(dst, src, lib, 0);
}
