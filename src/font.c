#include "internal.h"
#include "font_stroke.h"

#include <ctype.h>

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

static void fold_name(const char *name, char *buf, size_t cap) {
    size_t i, n;
    n = fc_strlen(name);
    if (n >= cap) n = cap - 1;
    for (i = 0; i < n; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        buf[i] = c;
    }
    buf[n] = 0;
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

int fc_font_is_serif(const char *name) {
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
    if (fc_font_is_serif(name)) return 1;
    return 0;
}

static int extra_index(int cp) {
    int i;
    for (i = 0; i < FC_STROKE_NEXTRA; i++)
        if (fc_stroke_extra_cp[i] == cp) return i;
    return -1;
}

static const FcStrokeGlyph *lookup(int cp) {
    int e;
    if (cp >= 32 && cp <= 126) return &fc_stroke_ascii[cp - 32];
    e = extra_index(cp);
    if (e >= 0) return &fc_stroke_extra[e];
    return NULL;
}

int fc_font_decompose(int cp, int *base, int *acc) {
    /* acc: 0 none, 1 acute, 2 grave, 3 circ, 4 uml, 5 tilde, 6 cedilla */
    *acc = 0;
    *base = cp;
    switch (cp) {
    case 0xC1:
        *base = 'A';
        *acc = 1;
        return 1;
    case 0xC9:
        *base = 'E';
        *acc = 1;
        return 1;
    case 0xCD:
        *base = 'I';
        *acc = 1;
        return 1;
    case 0xD3:
        *base = 'O';
        *acc = 1;
        return 1;
    case 0xDA:
        *base = 'U';
        *acc = 1;
        return 1;
    case 0xE1:
        *base = 'a';
        *acc = 1;
        return 1;
    case 0xE9:
        *base = 'e';
        *acc = 1;
        return 1;
    case 0xED:
        *base = 'i';
        *acc = 1;
        return 1;
    case 0xF3:
        *base = 'o';
        *acc = 1;
        return 1;
    case 0xFA:
        *base = 'u';
        *acc = 1;
        return 1;
    case 0xC0:
        *base = 'A';
        *acc = 2;
        return 1;
    case 0xE0:
        *base = 'a';
        *acc = 2;
        return 1;
    case 0xC8:
        *base = 'E';
        *acc = 2;
        return 1;
    case 0xE8:
        *base = 'e';
        *acc = 2;
        return 1;
    case 0xCC:
        *base = 'I';
        *acc = 2;
        return 1;
    case 0xEC:
        *base = 'i';
        *acc = 2;
        return 1;
    case 0xD2:
        *base = 'O';
        *acc = 2;
        return 1;
    case 0xF2:
        *base = 'o';
        *acc = 2;
        return 1;
    case 0xD9:
        *base = 'U';
        *acc = 2;
        return 1;
    case 0xF9:
        *base = 'u';
        *acc = 2;
        return 1;
    case 0xC2:
        *base = 'A';
        *acc = 3;
        return 1;
    case 0xE2:
        *base = 'a';
        *acc = 3;
        return 1;
    case 0xCA:
        *base = 'E';
        *acc = 3;
        return 1;
    case 0xEA:
        *base = 'e';
        *acc = 3;
        return 1;
    case 0xCE:
        *base = 'I';
        *acc = 3;
        return 1;
    case 0xEE:
        *base = 'i';
        *acc = 3;
        return 1;
    case 0xD4:
        *base = 'O';
        *acc = 3;
        return 1;
    case 0xF4:
        *base = 'o';
        *acc = 3;
        return 1;
    case 0xDB:
        *base = 'U';
        *acc = 3;
        return 1;
    case 0xFB:
        *base = 'u';
        *acc = 3;
        return 1;
    case 0xC4:
        *base = 'A';
        *acc = 4;
        return 1;
    case 0xE4:
        *base = 'a';
        *acc = 4;
        return 1;
    case 0xCB:
        *base = 'E';
        *acc = 4;
        return 1;
    case 0xEB:
        *base = 'e';
        *acc = 4;
        return 1;
    case 0xCF:
        *base = 'I';
        *acc = 4;
        return 1;
    case 0xEF:
        *base = 'i';
        *acc = 4;
        return 1;
    case 0xD6:
        *base = 'O';
        *acc = 4;
        return 1;
    case 0xF6:
        *base = 'o';
        *acc = 4;
        return 1;
    case 0xDC:
        *base = 'U';
        *acc = 4;
        return 1;
    case 0xFC:
        *base = 'u';
        *acc = 4;
        return 1;
    case 0xD1:
        *base = 'N';
        *acc = 5;
        return 1;
    case 0xF1:
        *base = 'n';
        *acc = 5;
        return 1;
    case 0xC7:
        *base = 'C';
        *acc = 6;
        return 1;
    case 0xE7:
        *base = 'c';
        *acc = 6;
        return 1;
    case 0xBF:
        *base = 0xBF;
        return 1; /* ¿ stored as extra */
    case 0xA1:
        *base = 0xA1;
        return 1;
    case 0xB0:
        *base = 0xB0;
        return 1;
    default:
        return lookup(cp) != NULL || (cp >= 32 && cp <= 126);
    }
}

int fc_stroke_lr(int cp, int *left, int *right) {
    const FcStrokeGlyph *g;
    int base, acc;
    fc_font_decompose(cp, &base, &acc);
    g = lookup(base);
    if (!g) g = lookup('?');
    if (!g) return 0;
    *left = g->left;
    *right = g->right;
    return 1;
}

float fc_stroke_advance(int cp, int mono) {
    int l, r;
    if (cp == '\n' || cp == '\r') return 0;
    if (!fc_stroke_lr(cp, &l, &r)) {
        l = 0;
        r = 8;
    }
    if (mono) return 10.0f;
    return (float)(r - l);
}

int fc_stroke_points(int cp, int8_t *xy, int maxn) {
    const FcStrokeGlyph *g;
    int base, acc, n, i;
    fc_font_decompose(cp, &base, &acc);
    g = lookup(base);
    if (!g) g = lookup('?');
    if (!g) return 0;
    n = g->n;
    if (n > maxn) n = maxn;
    memcpy(xy, fc_stroke_xy + g->off, (size_t)n);
    /* For i/j with an accent, drop the dot (last pen-up + two points if tiny). */
    if ((base == 'i' || base == 'j') && acc) {
        /* keep the stem only: first polyline */
        for (i = 0; i < n - 1; i += 2) {
            if ((unsigned char)xy[i] == FC_STROKE_PU || (unsigned char)xy[i + 1] == FC_STROKE_PU) {
                n = i;
                break;
            }
        }
    }
    (void)acc;
    return n;
}

int fc_stroke_accent(int cp) {
    int base, acc;
    fc_font_decompose(cp, &base, &acc);
    return acc;
}
