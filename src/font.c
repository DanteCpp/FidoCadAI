#include "internal.h"
#include "font_stroke.h"

int fc_utf8_next(const char **p, const char *end) {
    const unsigned char *s;
    int cp;
    if (!p || !*p || *p >= end) return 0;
    s = (const unsigned char *)*p;
    if (s[0] < 0x80) {
        *p += 1;
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && *p + 1 < end && (s[1] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *p += 2;
        return cp;
    }
    if ((s[0] & 0xF0) == 0xE0 && *p + 2 < end && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *p += 3;
        return cp;
    }
    *p += 1;
    return '?';
}

/* Lowercased copy of a font name, cut to cap - 1 bytes. */
static void fold_name(const char *name, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", name ? name : "");
    fc_tolower_buf(buf);
}

int fc_font_is_mono(const char *name) {
    char buf[64];
    if (!name || !name[0] || (name[0] == '*' && name[1] == 0)) return 1;
    if (fc_streq(name, "Courier New") || fc_streq(name, "Courier")) return 1;
    fold_name(name, buf, sizeof buf);
    if (strstr(buf, "courier") || strstr(buf, "mono") || strstr(buf, "consolas") ||
        strstr(buf, "menlo") || strstr(buf, "lucida console"))
        return 1;
    return 0;
}

static int font_is_serif(const char *name) {
    char buf[64];
    if (!name || !name[0] || (name[0] == '*' && name[1] == 0)) return 0;
    fold_name(name, buf, sizeof buf);
    if (strstr(buf, "serif") && !strstr(buf, "sans")) return 1;
    if (strstr(buf, "times") || strstr(buf, "constantia") || strstr(buf, "georgia") ||
        strstr(buf, "cambria") || strstr(buf, "palatino") || strstr(buf, "garamond") ||
        strstr(buf, "roman") || strstr(buf, "liberation serif"))
        return 1;
    return 0;
}

int fc_font_family(const char *name) {
    if (fc_font_is_mono(name)) return 2;
    if (font_is_serif(name)) return 1;
    return 0;
}

static const FcStrokeGlyph *lookup(int cp) {
    int i;
    if (cp >= 32 && cp <= 126) return &fc_stroke_ascii[cp - 32];
    for (i = 0; i < FC_STROKE_NEXTRA; i++)
        if (fc_stroke_extra_cp[i] == cp) return &fc_stroke_extra[i];
    return NULL;
}

/* Accented Latin-1 letters: drawn as the base letter plus an accent mark
 * (1 acute, 2 grave, 3 circumflex, 4 umlaut, 5 tilde, 6 cedilla). */
static const struct {
    unsigned char cp, base, accent;
} k_accented[] = {
    {0xC1, 'A', 1}, {0xC9, 'E', 1}, {0xCD, 'I', 1}, {0xD3, 'O', 1}, {0xDA, 'U', 1},
    {0xE1, 'a', 1}, {0xE9, 'e', 1}, {0xED, 'i', 1}, {0xF3, 'o', 1}, {0xFA, 'u', 1},
    {0xC0, 'A', 2}, {0xC8, 'E', 2}, {0xCC, 'I', 2}, {0xD2, 'O', 2}, {0xD9, 'U', 2},
    {0xE0, 'a', 2}, {0xE8, 'e', 2}, {0xEC, 'i', 2}, {0xF2, 'o', 2}, {0xF9, 'u', 2},
    {0xC2, 'A', 3}, {0xCA, 'E', 3}, {0xCE, 'I', 3}, {0xD4, 'O', 3}, {0xDB, 'U', 3},
    {0xE2, 'a', 3}, {0xEA, 'e', 3}, {0xEE, 'i', 3}, {0xF4, 'o', 3}, {0xFB, 'u', 3},
    {0xC4, 'A', 4}, {0xCB, 'E', 4}, {0xCF, 'I', 4}, {0xD6, 'O', 4}, {0xDC, 'U', 4},
    {0xE4, 'a', 4}, {0xEB, 'e', 4}, {0xEF, 'i', 4}, {0xF6, 'o', 4}, {0xFC, 'u', 4},
    {0xD1, 'N', 5}, {0xF1, 'n', 5}, {0xC7, 'C', 6}, {0xE7, 'c', 6},
};

int fc_stroke_glyph(int cp, FcStroke *out) {
    const FcStrokeGlyph *g;
    int i, base = cp;
    out->accent = 0;
    for (i = 0; i < (int)(sizeof k_accented / sizeof k_accented[0]); i++)
        if (k_accented[i].cp == cp) {
            base = k_accented[i].base;
            out->accent = k_accented[i].accent;
            break;
        }
    g = lookup(base);
    if (!g) g = lookup('?');
    out->left = g->left;
    out->right = g->right;
    out->xy = fc_stroke_xy + g->off;
    out->n = g->n;
    out->lower = base >= 'a' && base <= 'z';
    /* the accent replaces the dot of i and j: keep the stem, the first polyline */
    if ((base == 'i' || base == 'j') && out->accent)
        for (i = 0; i + 1 < out->n; i += 2)
            if ((unsigned char)out->xy[i] == FC_STROKE_PU ||
                (unsigned char)out->xy[i + 1] == FC_STROKE_PU) {
                out->n = i;
                break;
            }
    return 1;
}
