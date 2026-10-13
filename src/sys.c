/* Platform glue: where the running binary lives, and the data directories to search
 * relative to it. Everything else in the tree is plain C11 + POSIX. */
/* realpath() is declared by glibc under __USE_XOPEN_EXTENDED, not the plain POSIX
 * guard the Makefile sets, so ask for XSI here before any header is included. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "internal.h"

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char g_argv0[PATH_MAX];

void fc_set_argv0(const char *argv0) {
    if (argv0) snprintf(g_argv0, sizeof g_argv0, "%s", argv0);
}

void fc_dirname(const char *path, char *out, size_t cap) {
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

static int is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* argv[0] as the shell resolved it: a path when it has a '/', else the first match
 * on $PATH. */
static int exe_from_argv0(char *out, size_t cap) {
    char cand[PATH_MAX];
    const char *path, *p;
    if (!g_argv0[0]) return 0;
    if (strchr(g_argv0, '/')) return realpath(g_argv0, cand) && snprintf(out, cap, "%s", cand) > 0;
    path = getenv("PATH");
    for (p = path; p && *p;) {
        const char *e = strchr(p, ':');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char full[2 * PATH_MAX + 2];
        int w;
        if (n == 0)
            w = snprintf(full, sizeof full, "./%s", g_argv0);
        else
            w = snprintf(full, sizeof full, "%.*s/%s", (int)n, p, g_argv0);
        if (w > 0 && (size_t)w < sizeof full && is_file(full) && access(full, X_OK) == 0 &&
            realpath(full, cand))
            return snprintf(out, cap, "%s", cand) > 0;
        p = e ? e + 1 : NULL;
    }
    return 0;
}

/* FC_EXE_FROM_ARGV0 forces the portable fallback (used by CI to test it on Linux) */
int fc_exe_path(char *out, size_t cap) {
#if defined(FC_EXE_FROM_ARGV0)
#elif defined(__linux__)
    {
        ssize_t n = readlink("/proc/self/exe", out, cap - 1);
        if (n > 0 && (size_t)n < cap - 1) {
            out[n] = 0;
            return 1;
        }
    }
#elif defined(__APPLE__)
    {
        char raw[PATH_MAX], real[PATH_MAX];
        uint32_t size = sizeof raw;
        if (_NSGetExecutablePath(raw, &size) == 0 && realpath(raw, real)) {
            snprintf(out, cap, "%s", real);
            return 1;
        }
    }
#endif
    return exe_from_argv0(out, cap);
}

int fc_app_dir(char *out, size_t cap) {
    const char *root = getenv("FCAI_HOME");
    const char *home = getenv("HOME");
    int n;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (root && *root) n = snprintf(out, cap, "%s", root);
    else if (home && *home) n = snprintf(out, cap, "%s/.fcai", home);
    else return 0;
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return 0; }
    while (n > 1 && out[n - 1] == '/') out[--n] = 0;
    return 1;
}

int fc_data_dirs(const char *sub, char dirs[FC_DATA_DIRS][FC_DIR_MAX]) {
    static const char *const k_prefixes[] = {"/usr/local/share/fidocad", "/opt/homebrew/share/fidocad",
                                             "/usr/share/fidocad"};
    char exe[PATH_MAX], dir[PATH_MAX];
    int i, n = 0;
    if (fc_exe_path(exe, sizeof exe)) {
        /* installed ($PREFIX/bin + $PREFIX/share/fidocad) or a build tree (build/ + share/) */
        fc_dirname(exe, dir, sizeof dir);
        snprintf(dirs[n++], FC_DIR_MAX, "%s/../share/fidocad/%s", dir, sub);
        snprintf(dirs[n++], FC_DIR_MAX, "%s/../share/%s", dir, sub);
        snprintf(dirs[n++], FC_DIR_MAX, "%s/share/%s", dir, sub);
    }
    for (i = 0; i < 3; i++) snprintf(dirs[n++], FC_DIR_MAX, "%s/%s", k_prefixes[i], sub);
    snprintf(dirs[n++], FC_DIR_MAX, "share/%s", sub);
    return n;
}
