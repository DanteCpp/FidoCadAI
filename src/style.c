/* User drawing preferences. This is CLI state, never drawing/render state.
   Plain Markdown, bounded reads, a POSIX writer lock, atomic replacement,
   and immutable snapshots of the previous profile on every real change. */
#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define STYLE_MAX_BYTES 65536
#define STYLE_MAX_PREFS 256
#define STYLE_HEADER "# FidoCadAI style profile"

typedef struct {
    const char *key, *scope, *source, *updated, *text;
} StylePref;

typedef struct {
    char *raw, *storage;
    int learning, n;
    StylePref prefs[STYLE_MAX_PREFS];
} Style;

static void style_free(Style *p) {
    free(p->raw);
    free(p->storage);
    memset(p, 0, sizeof *p);
}

static int field_ok(const char *s, size_t max) {
    const unsigned char *q = (const unsigned char *)s;
    int nonspace = 0;
    if (!s || !*s || strlen(s) > max) return 0;
    for (; *q; q++) {
        if (*q < 32 || *q == 127) return 0;
        if (*q != ' ') nonspace = 1;
    }
    return nonspace;
}

static int key_ok(const char *s) {
    const char *q;
    if (!s || !*s || strlen(s) > 64 || s[0] == '.' || s[0] == '-' || s[0] == '_') return 0;
    for (q = s; *q; q++)
        if (!(*q >= 'a' && *q <= 'z') && !(*q >= '0' && *q <= '9') &&
            *q != '.' && *q != '-' && *q != '_') return 0;
    return 1;
}

static int style_index(const Style *p, const char *key) {
    int i;
    for (i = 0; i < p->n; i++) if (fc_streq(p->prefs[i].key, key)) return i;
    return -1;
}

static char *next_line(char **cursor) {
    char *s = *cursor, *end;
    size_t n;
    if (!s || !*s) return NULL;
    end = strchr(s, '\n');
    if (end) { *end = 0; *cursor = end + 1; }
    else *cursor = NULL;
    n = strlen(s);
    if (n && s[n - 1] == '\r') s[n - 1] = 0;
    return s;
}

static char *next_nonblank(char **cursor) {
    char *s;
    do { s = next_line(cursor); } while (s && !*s);
    return s;
}

static char *metadata(char **cursor, const char *prefix) {
    char *s = next_line(cursor);
    size_t n = strlen(prefix);
    return s && strncmp(s, prefix, n) == 0 ? s + n : NULL;
}

static int style_parse(Style *p) {
    char *cursor, *s;
    p->storage = strdup(p->raw);
    if (!p->storage) return FC_ERR_NOMEM;
    cursor = p->storage;
    if (!fc_streq(next_line(&cursor), STYLE_HEADER)) return FC_ERR_ARG;
    s = next_nonblank(&cursor);
    if (fc_streq(s, "Learning: on")) p->learning = 1;
    else if (!fc_streq(s, "Learning: off")) return FC_ERR_ARG;
    while ((s = next_nonblank(&cursor))) {
        StylePref *v;
        char *blank;
        if (strncmp(s, "## ", 3) || !key_ok(s + 3) ||
            p->n == STYLE_MAX_PREFS || style_index(p, s + 3) >= 0) return FC_ERR_ARG;
        v = &p->prefs[p->n];
        v->key = s + 3;
        v->scope = metadata(&cursor, "- Scope: ");
        v->source = metadata(&cursor, "- Source: ");
        v->updated = metadata(&cursor, "- Updated: ");
        blank = next_line(&cursor);
        if (!blank || *blank) return FC_ERR_ARG;
        v->text = next_line(&cursor);
        if (!field_ok(v->scope, 1024) || !field_ok(v->source, 1024) ||
            !field_ok(v->updated, 32) || !field_ok(v->text, 4096)) return FC_ERR_ARG;
        /* A paragraph is one line; require a separator before the next key. */
        blank = next_line(&cursor);
        if (blank && *blank) return FC_ERR_ARG;
        p->n++;
    }
    return FC_OK;
}

static int bounded_read(const char *path, char **out, int absent_ok) {
    FILE *f;
    char *buf;
    size_t n;
    int err;
    *out = NULL;
    f = fopen(path, "rb");
    if (!f) return absent_ok && errno == ENOENT ? FC_OK : FC_ERR_IO;
    buf = malloc(STYLE_MAX_BYTES + 1);
    if (!buf) { fclose(f); return FC_ERR_NOMEM; }
    n = fread(buf, 1, STYLE_MAX_BYTES + 1, f);
    err = ferror(f);
    fclose(f);
    if (err || n > STYLE_MAX_BYTES || memchr(buf, 0, n)) {
        free(buf);
        return err ? FC_ERR_IO : FC_ERR_ARG;
    }
    buf[n] = 0;
    *out = buf;
    return FC_OK;
}

static int style_load(const char *path, Style *p, int absent_ok) {
    int rc;
    memset(p, 0, sizeof *p);
    rc = bounded_read(path, &p->raw, absent_ok);
    if (rc == FC_OK && p->raw) rc = style_parse(p);
    if (rc != FC_OK) {
        fprintf(stderr, "fcai: cannot read style profile %s (%s)\n", path,
                rc == FC_ERR_ARG ? "invalid format or exceeds 64 KiB" : "I/O or allocation error");
        style_free(p);
    }
    return rc;
}

static char *style_markdown(const Style *p) {
    FcBuf b;
    int i;
    fc_buf_init(&b);
    /* Reserve the full limit so subsequent buffer appends cannot truncate on OOM. */
    if (fc_buf_reserve(&b, STYLE_MAX_BYTES + 1) != 0) return NULL;
    fc_buf_printf(&b, STYLE_HEADER "\n\nLearning: %s\n\n", p->learning ? "on" : "off");
    for (i = 0; i < p->n; i++) {
        const StylePref *v = &p->prefs[i];
        size_t need = strlen(v->key) + strlen(v->scope) + strlen(v->source) +
                      strlen(v->updated) + strlen(v->text) + 64;
        if (b.n + need > STYLE_MAX_BYTES) { fc_buf_free(&b); return NULL; }
        fc_buf_printf(&b, "## %s\n- Scope: %s\n- Source: %s\n- Updated: %s\n\n%s\n\n",
                      v->key, v->scope, v->source, v->updated, v->text);
    }
    return fc_buf_steal(&b);
}

static int style_path(char *root, size_t root_cap, char *path, size_t cap) {
    int n;
    if (!fc_app_dir(root, root_cap)) return 0;
    n = snprintf(path, cap, "%s/style.md", root);
    return n > 0 && (size_t)n < cap;
}

int fc_style_context(FcBuf *b) {
    char root[4096], path[4096];
    Style p;
    int i, rc;
    if (!style_path(root, sizeof root, path, sizeof path)) return FC_ERR_ARG;
    rc = style_load(path, &p, 1);
    if (rc != FC_OK) return rc;
    /* No profile yet: keep existing context output and do not create files. */
    if (p.raw) {
        fc_buf_printf(b, "=== Drawing style (%s) ===\nLearning: %s\n", path,
                      p.learning ? "on" : "off");
        fc_buf_puts(b, "Apply matching scopes as defaults; current instructions take precedence.\n");
        for (i = 0; i < p.n; i++) {
            const StylePref *v = &p.prefs[i];
            fc_buf_printf(b, "- %s [%s]: %s (source: %s)\n", v->key, v->scope, v->text, v->source);
        }
        fc_buf_putc(b, '\n');
    }
    style_free(&p);
    return FC_OK;
}

static int print_style(const Style *p, const char *path, int json) {
    char *text;
    FcBuf b;
    int i, rc;
    if (!json) {
        text = style_markdown(p);
        if (!text) return FC_ERR_NOMEM;
        rc = fc_write_file("-", text, strlen(text));
        free(text);
        return rc;
    }
    fc_buf_init(&b);
    if (fc_buf_reserve(&b, STYLE_MAX_BYTES * 6 + 8192) != 0) return FC_ERR_NOMEM;
    fc_buf_puts(&b, "{\"path\":"); fc_json_escape(&b, path);
    fc_buf_printf(&b, ",\"learning\":%s,\"preferences\":[", p->learning ? "true" : "false");
    for (i = 0; i < p->n; i++) {
        const StylePref *v = &p->prefs[i];
        if (i) fc_buf_putc(&b, ',');
        fc_buf_puts(&b, "{\"key\":"); fc_json_escape(&b, v->key);
        fc_buf_puts(&b, ",\"scope\":"); fc_json_escape(&b, v->scope);
        fc_buf_puts(&b, ",\"source\":"); fc_json_escape(&b, v->source);
        fc_buf_puts(&b, ",\"updated\":"); fc_json_escape(&b, v->updated);
        fc_buf_puts(&b, ",\"text\":"); fc_json_escape(&b, v->text);
        fc_buf_putc(&b, '}');
    }
    fc_buf_puts(&b, "]}\n");
    rc = fc_write_file("-", b.p, b.n);
    fc_buf_free(&b);
    return rc;
}

static int mkdir_private(const char *path) {
    char tmp[4096], *q;
    struct stat st;
    size_t n = strlen(path);
    if (!n || n >= sizeof tmp) return 0;
    memcpy(tmp, path, n + 1);
    for (q = tmp + 1;; q++) {
        int last = !*q;
        if (*q == '/' || last) {
            char c = *q;
            *q = 0;
            if ((mkdir(tmp, 0700) != 0 && errno != EEXIST) ||
                stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
            *q = c;
        }
        if (last) return 1;
    }
}

static int lock_profile(const char *root) {
    char path[4096];
    struct flock lock;
    int fd, n = snprintf(path, sizeof path, "%s/.style.lock", root);
    if (n < 0 || (size_t)n >= sizeof path || !mkdir_private(root)) return -1;
    fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;
    memset(&lock, 0, sizeof lock);
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    while (fcntl(fd, F_SETLKW, &lock) != 0) {
        if (errno == EINTR) continue;
        close(fd);
        return -1;
    }
    return fd;
}

static int utc_stamp(char *out, size_t cap, const char *format) {
    time_t now = time(NULL);
    struct tm t;
    return gmtime_r(&now, &t) && strftime(out, cap, format, &t) > 0;
}

/* mkstemp templates are inside the profile root, on the same filesystem. */
static int write_temp(char *path, const char *text) {
    int fd = mkstemp(path);
    size_t n = strlen(text), done = 0;
    if (fd < 0) return FC_ERR_IO;
    while (done < n) {
        ssize_t w = write(fd, text + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) { close(fd); unlink(path); return FC_ERR_IO; }
        done += (size_t)w;
    }
    if (fsync(fd) != 0) { close(fd); unlink(path); return FC_ERR_IO; }
    if (close(fd) != 0) { unlink(path); return FC_ERR_IO; }
    return FC_OK;
}

static int save_style(const char *root, const char *path, const Style *old, const Style *next) {
    char dir[4096], snapshot[4096], temp[4096], stamp[32];
    char *text = style_markdown(next), *empty = NULL;
    const char *previous = old->raw;
    int n, rc = FC_ERR_IO;
    if (!text) return FC_ERR_NOMEM;
    if (previous && fc_streq(previous, text)) { free(text); return FC_OK; }
    if (!previous) {
        empty = style_markdown(old);
        previous = empty;
        if (!empty) { free(text); return FC_ERR_NOMEM; }
    }
    n = snprintf(dir, sizeof dir, "%s/history/style", root);
    if (n < 0 || (size_t)n >= sizeof dir || !mkdir_private(dir) ||
        !utc_stamp(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ")) goto done;
    n = snprintf(snapshot, sizeof snapshot, "%s/%s-XXXXXX", dir, stamp);
    if (n < 0 || (size_t)n >= sizeof snapshot) goto done;
    n = snprintf(temp, sizeof temp, "%s/.style-XXXXXX", root);
    if (n < 0 || (size_t)n >= sizeof temp) goto done;
    /* Save the old file verbatim before replacing it. Failed writes retain it. */
    if (write_temp(snapshot, previous) != FC_OK) goto done;
    if (write_temp(temp, text) != FC_OK) { unlink(snapshot); goto done; }
    if (rename(temp, path) != 0) { unlink(temp); unlink(snapshot); goto done; }
    rc = FC_OK;
done:
    free(empty);
    free(text);
    if (rc != FC_OK) fprintf(stderr, "fcai: cannot save style profile %s\n", path);
    return rc;
}

static int history_id_ok(const char *s) {
    size_t i;
    if (!s || strlen(s) != 23 || s[8] != 'T' || s[15] != 'Z' || s[16] != '-') return 0;
    for (i = 0; i < 23; i++) {
        char c = s[i];
        if (i == 8 || i == 15 || i == 16) continue;
        if (i < 16) { if (c < '0' || c > '9') return 0; }
        else if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'z') &&
                 !(c >= 'A' && c <= 'Z')) return 0;
    }
    return 1;
}

static int compare_ids(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int print_history(const char *root) {
    char path[4096];
    DIR *dir;
    struct dirent *entry;
    char **ids = NULL;
    size_t n = 0, i;
    int w, rc = FC_OK;
    w = snprintf(path, sizeof path, "%s/history/style", root);
    if (w < 0 || (size_t)w >= sizeof path) return FC_ERR_ARG;
    dir = opendir(path);
    if (!dir) return errno == ENOENT ? FC_OK : FC_ERR_IO;
    while ((entry = readdir(dir))) {
        char **grown;
        if (!history_id_ok(entry->d_name)) continue;
        grown = realloc(ids, (n + 1) * sizeof *ids);
        if (!grown) { rc = FC_ERR_NOMEM; break; }
        ids = grown;
        ids[n] = strdup(entry->d_name);
        if (!ids[n]) { rc = FC_ERR_NOMEM; break; }
        n++;
    }
    closedir(dir);
    if (n > 1) qsort(ids, n, sizeof *ids, compare_ids);
    if (rc == FC_OK) for (i = 0; i < n; i++) puts(ids[i]);
    for (i = 0; i < n; i++) free(ids[i]);
    free(ids);
    return rc;
}

static void style_usage(void) {
    fputs("usage: fcai style [show [--json] | path | history]\n"
          "       fcai style set KEY TEXT [--scope SCOPE] [--source SOURCE] [--base FILE]\n"
          "       fcai style forget KEY [--base FILE]\n"
          "       fcai style learning on|off [--base FILE]\n"
          "       fcai style restore ID [--base FILE]\n"
          "User profile: ~/.fcai/style.md ($FCAI_HOME overrides ~/.fcai).\n"
          "Keys: lowercase letters, digits, dots, underscores and hyphens, max 64 bytes.\n"
          "Scope/source/text: nonempty single lines; defaults: all/user instruction.\n"
          "Learning controls agent capture; saved preferences still apply when off.\n"
          "Set/forget are explicit edits, allowed with learning off.\n"
          "--base compares a `style show` snapshot and rejects stale updates.\n"
          "History lists previous profiles; restore takes an ID from that list.\n", stdout);
}

int fc_cmd_style(int argc, char **argv) {
    const char *action = argc ? argv[0] : "show";
    const char *scope = NULL, *source = NULL, *base = NULL;
    const char *args[2];
    char root[4096], path[4096], updated[32];
    Style p, restored;
    int i, nargs = 0, json = 0, end_options = 0, fd = -1, rc = FC_ERR_ARG, idx, mutating;
    if (fc_streq(action, "--help") || fc_streq(action, "help")) { style_usage(); return 0; }
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (end_options) {
            if (nargs == 2) goto usage;
            args[nargs++] = a;
        }
        else if (fc_streq(a, "--")) end_options = 1;
        else if (fc_streq(a, "--json")) json = 1;
        else if (fc_streq(a, "--scope") && i + 1 < argc) scope = argv[++i];
        else if (fc_streq(a, "--source") && i + 1 < argc) source = argv[++i];
        else if (fc_streq(a, "--base") && i + 1 < argc) base = argv[++i];
        else if (a[0] != '-' && nargs < 2) args[nargs++] = a;
        else goto usage;
    }
    mutating = fc_streq(action, "set") || fc_streq(action, "forget") ||
               fc_streq(action, "learning") || fc_streq(action, "restore");
    if (fc_streq(action, "set")) {
        if (nargs != 2 || json || !key_ok(args[0]) || !field_ok(args[1], 4096) ||
            (scope && !field_ok(scope, 1024)) || (source && !field_ok(source, 1024))) goto usage;
    } else {
        if (scope || source || (base && !mutating) || (json && !fc_streq(action, "show"))) goto usage;
        if (fc_streq(action, "forget")) { if (nargs != 1 || !key_ok(args[0])) goto usage; }
        else if (fc_streq(action, "learning")) {
            if (nargs != 1 || (!fc_streq(args[0], "on") && !fc_streq(args[0], "off"))) goto usage;
        } else if (fc_streq(action, "restore")) { if (nargs != 1 || !history_id_ok(args[0])) goto usage; }
        else if ((!fc_streq(action, "show") && !fc_streq(action, "path") &&
                  !fc_streq(action, "history")) || nargs) goto usage;
    }
    if (!style_path(root, sizeof root, path, sizeof path)) {
        fprintf(stderr, "fcai: cannot locate profile; set HOME or FCAI_HOME\n"); return 1;
    }
    if (fc_streq(action, "path")) { puts(path); return 0; }
    if (fc_streq(action, "history")) return print_history(root) == FC_OK ? 0 : 1;
    if (mutating) {
        fd = lock_profile(root);
        if (fd < 0) { fprintf(stderr, "fcai: cannot lock style profile %s\n", path); return 1; }
    }
    rc = style_load(path, &p, 1);
    if (rc != FC_OK) { if (fd >= 0) close(fd); return 1; }
    if (base) {
        Style baseline;
        char *current = style_markdown(&p), *expected = NULL;
        rc = style_load(base, &baseline, 0);
        if (rc == FC_OK) expected = style_markdown(&baseline);
        if (rc != FC_OK || !current || !expected || !fc_streq(current, expected)) {
            fprintf(stderr, "fcai: stale or invalid style base; read fresh preferences and merge\n");
            rc = FC_ERR_ARG;
        }
        style_free(&baseline);
        free(current); free(expected);
        if (rc != FC_OK) goto done;
    }
    if (fc_streq(action, "show")) rc = print_style(&p, path, json);
    else if (fc_streq(action, "restore")) {
        char snapshot[4096];
        int n = snprintf(snapshot, sizeof snapshot, "%s/history/style/%s", root, args[0]);
        if (n < 0 || (size_t)n >= sizeof snapshot) { rc = FC_ERR_ARG; goto done; }
        rc = style_load(snapshot, &restored, 0);
        if (rc == FC_OK) rc = save_style(root, path, &p, &restored);
        style_free(&restored);
    } else {
        Style next = p; /* Fields point into the old profile or argv until saved. */
        if (fc_streq(action, "learning")) {
            next.learning = fc_streq(args[0], "on");
            if (p.raw && next.learning == p.learning) { rc = FC_OK; goto done; }
        }
        else if (fc_streq(action, "forget")) {
            idx = style_index(&next, args[0]);
            if (idx < 0) { rc = FC_OK; goto done; }
            memmove(&next.prefs[idx], &next.prefs[idx + 1],
                    (size_t)(next.n - idx - 1) * sizeof next.prefs[0]);
            next.n--;
        } else {
            StylePref *v;
            idx = style_index(&next, args[0]);
            if (idx < 0) {
                if (next.n == STYLE_MAX_PREFS) { rc = FC_ERR_ARG; goto done; }
                idx = next.n++;
                next.prefs[idx] = (StylePref){args[0], "all", "user instruction", "", ""};
            }
            v = &next.prefs[idx];
            if ((!scope || fc_streq(scope, v->scope)) && (!source || fc_streq(source, v->source)) &&
                fc_streq(args[1], v->text)) { rc = FC_OK; goto done; }
            if (!utc_stamp(updated, sizeof updated, "%Y-%m-%dT%H:%M:%SZ")) { rc = FC_ERR_IO; goto done; }
            if (scope) v->scope = scope;
            if (source) v->source = source;
            v->text = args[1];
            v->updated = updated;
        }
        rc = save_style(root, path, &p, &next);
    }
done:
    style_free(&p);
    if (fd >= 0) close(fd);
    return rc == FC_OK ? 0 : 1;
usage:
    fprintf(stderr, "fcai: invalid style command or fields; use `fcai style --help`\n");
    return 2;
}
