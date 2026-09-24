#include "internal.h"
#include "font8x8.h"
#include "font_math.h"

#include <math.h>

enum {
    MG_ALPHA,
    MG_BETA,
    MG_GAMMA,
    MG_DELTA,
    MG_EPS,
    MG_ZETA,
    MG_ETA,
    MG_THETA,
    MG_IOTA,
    MG_KAPPA,
    MG_LAM,
    MG_MU,
    MG_NU,
    MG_XI,
    MG_PI,
    MG_RHO,
    MG_SIGMA,
    MG_TAU,
    MG_UPS,
    MG_PHI,
    MG_CHI,
    MG_PSI,
    MG_OMEGA,
    MG_GAMMA_U,
    MG_DELTA_U,
    MG_THETA_U,
    MG_LAM_U,
    MG_XI_U,
    MG_PI_U,
    MG_SIGMA_U,
    MG_PHI_U,
    MG_PSI_U,
    MG_OMEGA_U,
    MG_PM,
    MG_CDOT,
    MG_TIMES,
    MG_DIV,
    MG_CIRC,
    MG_INF,
    MG_LEQ,
    MG_GEQ,
    MG_NEQ,
    MG_APPROX,
    MG_EQUIV,
    MG_PARTIAL,
    MG_NABLA,
    MG_INT,
    MG_ELLIPSIS,
    MG_DEGREE,
    MG_LARR,
    MG_RARR,
    MG_SQRT,
    MG_PRIME,
    MG_COUNT
};

#define MG(x) (128 + (x))

typedef enum { N_LIST, N_SYM, N_FRAC, N_SQRT, N_OVER, N_SPACE, N_SCRIPTS } NKind;

typedef struct Node {
    NKind kind;
    int ch;
    int italic, bold;
    int display;
    float space_em;
    struct Node *a, *b, *c, *next;
} Node;

typedef struct {
    const char *p;
    FcArena *arena;
    int err;
    int display;
    int italic;
    int bold;
    int roman;
} P;

typedef enum {
    CMD_SYM,
    CMD_FRAC,
    CMD_SQRT,
    CMD_OVER,
    CMD_STYLE,
    CMD_SPACE,
    CMD_FUNC,
    CMD_LEFT,
    CMD_SKIP
} CmdKind;

enum { ST_ROMAN = 1, ST_BOLD = 2, ST_ITAL = 4, ST_UP = 8 };

typedef struct {
    const char *name;
    CmdKind kind;
    int glyph;
    float space;
    int style;
} Cmd;

static const Cmd k_cmd[] = {
    {"alpha", CMD_SYM, MG(MG_ALPHA), 0, 0},
    {"beta", CMD_SYM, MG(MG_BETA), 0, 0},
    {"gamma", CMD_SYM, MG(MG_GAMMA), 0, 0},
    {"delta", CMD_SYM, MG(MG_DELTA), 0, 0},
    {"epsilon", CMD_SYM, MG(MG_EPS), 0, 0},
    {"varepsilon", CMD_SYM, MG(MG_EPS), 0, 0},
    {"zeta", CMD_SYM, MG(MG_ZETA), 0, 0},
    {"eta", CMD_SYM, MG(MG_ETA), 0, 0},
    {"theta", CMD_SYM, MG(MG_THETA), 0, 0},
    {"vartheta", CMD_SYM, MG(MG_THETA), 0, 0},
    {"iota", CMD_SYM, MG(MG_IOTA), 0, 0},
    {"kappa", CMD_SYM, MG(MG_KAPPA), 0, 0},
    {"lambda", CMD_SYM, MG(MG_LAM), 0, 0},
    {"mu", CMD_SYM, MG(MG_MU), 0, 0},
    {"nu", CMD_SYM, MG(MG_NU), 0, 0},
    {"xi", CMD_SYM, MG(MG_XI), 0, 0},
    {"pi", CMD_SYM, MG(MG_PI), 0, 0},
    {"varpi", CMD_SYM, MG(MG_PI), 0, 0},
    {"rho", CMD_SYM, MG(MG_RHO), 0, 0},
    {"sigma", CMD_SYM, MG(MG_SIGMA), 0, 0},
    {"tau", CMD_SYM, MG(MG_TAU), 0, 0},
    {"upsilon", CMD_SYM, MG(MG_UPS), 0, 0},
    {"phi", CMD_SYM, MG(MG_PHI), 0, 0},
    {"varphi", CMD_SYM, MG(MG_PHI), 0, 0},
    {"chi", CMD_SYM, MG(MG_CHI), 0, 0},
    {"psi", CMD_SYM, MG(MG_PSI), 0, 0},
    {"omega", CMD_SYM, MG(MG_OMEGA), 0, 0},
    {"Gamma", CMD_SYM, MG(MG_GAMMA_U), 0, 0},
    {"Delta", CMD_SYM, MG(MG_DELTA_U), 0, 0},
    {"Theta", CMD_SYM, MG(MG_THETA_U), 0, 0},
    {"Lambda", CMD_SYM, MG(MG_LAM_U), 0, 0},
    {"Xi", CMD_SYM, MG(MG_XI_U), 0, 0},
    {"Pi", CMD_SYM, MG(MG_PI_U), 0, 0},
    {"Sigma", CMD_SYM, MG(MG_SIGMA_U), 0, 0},
    {"Phi", CMD_SYM, MG(MG_PHI_U), 0, 0},
    {"Psi", CMD_SYM, MG(MG_PSI_U), 0, 0},
    {"Omega", CMD_SYM, MG(MG_OMEGA_U), 0, 0},
    {"ohm", CMD_SYM, MG(MG_OMEGA_U), 0, 0},
    {"pm", CMD_SYM, MG(MG_PM), 0, 0},
    {"mp", CMD_SYM, MG(MG_PM), 0, 0},
    {"cdot", CMD_SYM, MG(MG_CDOT), 0, 0},
    {"times", CMD_SYM, MG(MG_TIMES), 0, 0},
    {"div", CMD_SYM, MG(MG_DIV), 0, 0},
    {"circ", CMD_SYM, MG(MG_CIRC), 0, 0},
    {"degree", CMD_SYM, MG(MG_DEGREE), 0, 0},
    {"infty", CMD_SYM, MG(MG_INF), 0, 0},
    {"inf", CMD_SYM, MG(MG_INF), 0, 0},
    {"leq", CMD_SYM, MG(MG_LEQ), 0, 0},
    {"le", CMD_SYM, MG(MG_LEQ), 0, 0},
    {"geq", CMD_SYM, MG(MG_GEQ), 0, 0},
    {"ge", CMD_SYM, MG(MG_GEQ), 0, 0},
    {"neq", CMD_SYM, MG(MG_NEQ), 0, 0},
    {"ne", CMD_SYM, MG(MG_NEQ), 0, 0},
    {"approx", CMD_SYM, MG(MG_APPROX), 0, 0},
    {"equiv", CMD_SYM, MG(MG_EQUIV), 0, 0},
    {"partial", CMD_SYM, MG(MG_PARTIAL), 0, 0},
    {"nabla", CMD_SYM, MG(MG_NABLA), 0, 0},
    {"int", CMD_SYM, MG(MG_INT), 0, 0},
    {"sum", CMD_SYM, MG(MG_SIGMA_U), 0, 0},
    {"prod", CMD_SYM, MG(MG_PI_U), 0, 0},
    {"ldots", CMD_SYM, MG(MG_ELLIPSIS), 0, 0},
    {"dots", CMD_SYM, MG(MG_ELLIPSIS), 0, 0},
    {"cdots", CMD_SYM, MG(MG_ELLIPSIS), 0, 0},
    {"to", CMD_SYM, MG(MG_RARR), 0, 0},
    {"rightarrow", CMD_SYM, MG(MG_RARR), 0, 0},
    {"leftarrow", CMD_SYM, MG(MG_LARR), 0, 0},
    {"prime", CMD_SYM, MG(MG_PRIME), 0, 0},
    {"frac", CMD_FRAC, 0, 0, 0},
    {"dfrac", CMD_FRAC, 0, 0, 0},
    {"tfrac", CMD_FRAC, 0, 0, 0},
    {"sqrt", CMD_SQRT, 0, 0, 0},
    {"overline", CMD_OVER, 0, 0, 0},
    {"bar", CMD_OVER, 0, 0, 0},
    {"vec", CMD_OVER, 0, 0, 0},
    {"hat", CMD_OVER, 0, 0, 0},
    {"mathrm", CMD_STYLE, 0, 0, ST_ROMAN},
    {"textrm", CMD_STYLE, 0, 0, ST_ROMAN},
    {"rm", CMD_STYLE, 0, 0, ST_ROMAN},
    {"text", CMD_STYLE, 0, 0, ST_ROMAN | ST_UP},
    {"operatorname", CMD_STYLE, 0, 0, ST_ROMAN | ST_UP},
    {"mathbf", CMD_STYLE, 0, 0, ST_BOLD},
    {"textbf", CMD_STYLE, 0, 0, ST_BOLD},
    {"bf", CMD_STYLE, 0, 0, ST_BOLD},
    {"mathit", CMD_STYLE, 0, 0, ST_ITAL},
    {"textit", CMD_STYLE, 0, 0, ST_ITAL},
    {"it", CMD_STYLE, 0, 0, ST_ITAL},
    {",", CMD_SPACE, 0, 0.17f, 0},
    {":", CMD_SPACE, 0, 0.22f, 0},
    {";", CMD_SPACE, 0, 0.28f, 0},
    {"!", CMD_SPACE, 0, -0.17f, 0},
    {"quad", CMD_SPACE, 0, 1.0f, 0},
    {"qquad", CMD_SPACE, 0, 2.0f, 0},
    {" ", CMD_SPACE, 0, 0.35f, 0},
    {"sin", CMD_FUNC, 0, 0, 0},
    {"cos", CMD_FUNC, 0, 0, 0},
    {"tan", CMD_FUNC, 0, 0, 0},
    {"log", CMD_FUNC, 0, 0, 0},
    {"ln", CMD_FUNC, 0, 0, 0},
    {"exp", CMD_FUNC, 0, 0, 0},
    {"min", CMD_FUNC, 0, 0, 0},
    {"max", CMD_FUNC, 0, 0, 0},
    {"lim", CMD_FUNC, 0, 0, 0},
    {"arg", CMD_FUNC, 0, 0, 0},
    {"det", CMD_FUNC, 0, 0, 0},
    {"left", CMD_LEFT, 0, 0, 0},
    {"right", CMD_LEFT, 0, 0, 0},
    {"big", CMD_LEFT, 0, 0, 0},
    {"Big", CMD_LEFT, 0, 0, 0},
    {"bigg", CMD_LEFT, 0, 0, 0},
    {"Bigg", CMD_LEFT, 0, 0, 0},
    {"displaystyle", CMD_SKIP, 0, 0, 0},
    {"textstyle", CMD_SKIP, 0, 0, 0},
    {"scriptstyle", CMD_SKIP, 0, 0, 0},
    {NULL, CMD_SYM, 0, 0, 0}};

int fc_tex_has_math(const char *s) { return s && strchr(s, '$') != NULL; }

const unsigned char *fc_tex_glyph_bits(int gid) {
    if (gid >= 32 && gid <= 126) return fc_font8x8[gid - 32];
    if (gid >= 128 && gid < 128 + MG_COUNT) return fc_font8x8_math[gid - 128];
    return fc_font8x8['?' - 32];
}

int fc_tex_split(const char *text, FcTexSeg *out, int maxn) {
    int n = 0;
    const char *p = text;
    if (!text) return 0;
    while (*p && n < maxn) {
        const char *d1 = strchr(p, '$');
        if (!d1) {
            out[n].math = 0;
            out[n].p = p;
            out[n].n = (int)fc_strlen(p);
            n++;
            break;
        }
        if (d1 > p) {
            out[n].math = 0;
            out[n].p = p;
            out[n].n = (int)(d1 - p);
            n++;
            if (n >= maxn) break;
        }
        if (d1[1] == '$') {
            const char *end = strstr(d1 + 2, "$$");
            if (!end) {
                out[n].math = 0;
                out[n].p = d1;
                out[n].n = 2;
                n++;
                p = d1 + 2;
                continue;
            }
            out[n].math = 2;
            out[n].p = d1 + 2;
            out[n].n = (int)(end - (d1 + 2));
            n++;
            p = end + 2;
        } else {
            const char *end = strchr(d1 + 1, '$');
            if (!end) {
                out[n].math = 0;
                out[n].p = d1;
                out[n].n = 1;
                n++;
                p = d1 + 1;
                continue;
            }
            out[n].math = 1;
            out[n].p = d1 + 1;
            out[n].n = (int)(end - (d1 + 1));
            n++;
            p = end + 1;
        }
    }
    return n;
}

static Node *node_new(P *p, NKind k) {
    Node *n = (Node *)fc_arena_alloc(p->arena, sizeof *n);
    if (!n) {
        p->err = 1;
        return NULL;
    }
    memset(n, 0, sizeof *n);
    n->kind = k;
    n->italic = p->roman ? 0 : p->italic;
    n->bold = p->bold;
    n->display = p->display;
    return n;
}

static void skip_sp(P *p) {
    while (*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r') p->p++;
}

static const Cmd *find_cmd(const char *name) {
    int i;
    for (i = 0; k_cmd[i].name; i++)
        if (strcmp(k_cmd[i].name, name) == 0) return &k_cmd[i];
    return NULL;
}

static Node *parse_list(P *p, int stop);
static Node *parse_nucleus(P *p);
static Node *parse_item(P *p);

static Node *parse_group(P *p) {
    skip_sp(p);
    if (*p->p == '{') {
        Node *n;
        p->p++;
        n = parse_list(p, '}');
        if (*p->p == '}') p->p++;
        else p->err = 1;
        return n;
    }
    return parse_nucleus(p);
}

static Node *node_word(P *p, const char *w, int italic) {
    Node *list = node_new(p, N_LIST);
    Node **tail;
    int save_i, save_r;
    if (!list) return NULL;
    tail = &list->a;
    save_i = p->italic;
    save_r = p->roman;
    p->italic = italic;
    p->roman = !italic;
    for (; *w; w++) {
        Node *s = node_new(p, N_SYM);
        if (!s) break;
        s->ch = (unsigned char)*w;
        s->italic = italic;
        *tail = s;
        tail = &s->next;
    }
    p->italic = save_i;
    p->roman = save_r;
    return list;
}

static int utf8_sym(P *p) {
    const unsigned char *s = (const unsigned char *)p->p;
    unsigned int cp;
    if (s[0] == 0xCE && s[1]) {
        cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        p->p += 2;
        switch (cp) {
        case 0x3B1:
            return MG(MG_ALPHA);
        case 0x3B2:
            return MG(MG_BETA);
        case 0x3B3:
            return MG(MG_GAMMA);
        case 0x3B4:
            return MG(MG_DELTA);
        case 0x3B5:
            return MG(MG_EPS);
        case 0x3B8:
            return MG(MG_THETA);
        case 0x3BB:
            return MG(MG_LAM);
        case 0x3BC:
            return MG(MG_MU);
        case 0x393:
            return MG(MG_GAMMA_U);
        case 0x394:
            return MG(MG_DELTA_U);
        case 0x398:
            return MG(MG_THETA_U);
        case 0x39B:
            return MG(MG_LAM_U);
        case 0x3A0:
            return MG(MG_PI_U);
        case 0x3A3:
            return MG(MG_SIGMA_U);
        case 0x3A6:
            return MG(MG_PHI_U);
        case 0x3A8:
            return MG(MG_PSI_U);
        case 0x3A9:
            return MG(MG_OMEGA_U);
        default:
            return '?';
        }
    }
    if (s[0] == 0xCF && s[1]) {
        cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        p->p += 2;
        switch (cp) {
        case 0x3C0:
            return MG(MG_PI);
        case 0x3C1:
            return MG(MG_RHO);
        case 0x3C3:
            return MG(MG_SIGMA);
        case 0x3C4:
            return MG(MG_TAU);
        case 0x3C6:
            return MG(MG_PHI);
        case 0x3C7:
            return MG(MG_CHI);
        case 0x3C8:
            return MG(MG_PSI);
        case 0x3C9:
            return MG(MG_OMEGA);
        default:
            return '?';
        }
    }
    if (s[0] == 0xC2 && s[1] == 0xB0) {
        p->p += 2;
        return MG(MG_DEGREE);
    }
    if (s[0] == 0xC2 && s[1] == 0xB1) {
        p->p += 2;
        return MG(MG_PM);
    }
    if (s[0] == 0xC3 && s[1] == 0x97) {
        p->p += 2;
        return MG(MG_TIMES);
    }
    if (s[0] == 0xE2 && s[1] == 0x88 && s[2] == 0x9E) {
        p->p += 3;
        return MG(MG_INF);
    }
    if (s[0] == 0xE2 && s[1] == 0x86 && s[2] == 0x92) {
        p->p += 3;
        return MG(MG_RARR);
    }
    if (s[0] == 0xE2 && s[1] == 0x88 && s[2] == 0x9A) {
        p->p += 3;
        return MG(MG_SQRT);
    }
    return 0;
}

static Node *parse_command(P *p) {
    char name[32];
    int ni = 0;
    const Cmd *c;
    p->p++; /* skip backslash */
    if (!*p->p) return NULL;
    if ((*p->p >= 'a' && *p->p <= 'z') || (*p->p >= 'A' && *p->p <= 'Z')) {
        while (((*p->p >= 'a' && *p->p <= 'z') || (*p->p >= 'A' && *p->p <= 'Z')) && ni < 31)
            name[ni++] = *p->p++;
        name[ni] = 0;
        skip_sp(p);
    } else {
        name[0] = *p->p++;
        name[1] = 0;
    }
    c = find_cmd(name);
    if (!c) return node_word(p, name, 0);
    if (c->kind == CMD_SYM) {
        Node *n = node_new(p, N_SYM);
        if (n) {
            n->ch = c->glyph;
            n->italic = 0;
        }
        return n;
    }
    if (c->kind == CMD_SPACE) {
        Node *n = node_new(p, N_SPACE);
        if (n) n->space_em = c->space;
        return n;
    }
    if (c->kind == CMD_FUNC) return node_word(p, name, 0);
    if (c->kind == CMD_SKIP) return node_new(p, N_SPACE);
    if (c->kind == CMD_LEFT) {
        skip_sp(p);
        if (*p->p == '.') {
            p->p++;
            return node_new(p, N_SPACE);
        }
        return parse_nucleus(p);
    }
    if (c->kind == CMD_FRAC) {
        Node *n = node_new(p, N_FRAC);
        if (!n) return NULL;
        n->a = parse_group(p);
        n->b = parse_group(p);
        n->display = p->display || strcmp(name, "dfrac") == 0;
        return n;
    }
    if (c->kind == CMD_SQRT) {
        Node *n = node_new(p, N_SQRT);
        if (!n) return NULL;
        skip_sp(p);
        if (*p->p == '[') {
            p->p++;
            n->c = parse_list(p, ']');
            if (*p->p == ']') p->p++;
        }
        n->a = parse_group(p);
        return n;
    }
    if (c->kind == CMD_OVER) {
        Node *n = node_new(p, N_OVER);
        if (!n) return NULL;
        n->a = parse_group(p);
        return n;
    }
    if (c->kind == CMD_STYLE) {
        int si = p->italic, sr = p->roman, sb = p->bold;
        Node *n;
        if (c->style & ST_ROMAN) {
            p->roman = 1;
            p->italic = 0;
        }
        if (c->style & ST_BOLD) p->bold = 1;
        if (c->style & ST_ITAL) {
            p->italic = 1;
            p->roman = 0;
        }
        n = parse_group(p);
        p->italic = si;
        p->roman = sr;
        p->bold = sb;
        return n;
    }
    return NULL;
}

static Node *parse_nucleus(P *p) {
    int u;
    skip_sp(p);
    if (!*p->p || *p->p == '}' || *p->p == '$') return NULL;
    if (*p->p == '{') return parse_group(p);
    if (*p->p == '\\') return parse_command(p);
    if (*p->p == '~') {
        Node *n;
        p->p++;
        n = node_new(p, N_SPACE);
        if (n) n->space_em = 0.4f;
        return n;
    }
    if ((unsigned char)*p->p >= 0x80) {
        u = utf8_sym(p);
        if (u) {
            Node *n = node_new(p, N_SYM);
            if (n) {
                n->ch = u;
                n->italic = 0;
            }
            return n;
        }
        p->p++;
        return NULL;
    }
    {
        Node *n = node_new(p, N_SYM);
        unsigned char ch = (unsigned char)*p->p++;
        if (!n) return NULL;
        n->ch = ch;
        if (ch >= '0' && ch <= '9') n->italic = 0;
        if (ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '.' || ch == ',' ||
            ch == '=' || ch == '+' || ch == '-' || ch == '/' || ch == '|')
            n->italic = 0;
        return n;
    }
}

static Node *parse_item(P *p) {
    Node *nuc, *sup = NULL, *sub = NULL;
    skip_sp(p);
    if (!*p->p || *p->p == '}' || *p->p == '$') return NULL;
    nuc = parse_nucleus(p);
    if (!nuc) return NULL;
    for (;;) {
        skip_sp(p);
        if (*p->p == '^' && !sup) {
            p->p++;
            sup = parse_group(p);
            continue;
        }
        if (*p->p == '_' && !sub) {
            p->p++;
            sub = parse_group(p);
            continue;
        }
        if (*p->p == '\'') {
            Node *pr = node_new(p, N_SYM);
            p->p++;
            if (pr) {
                pr->ch = MG(MG_PRIME);
                pr->italic = 0;
            }
            if (!sup) sup = pr;
            else {
                /* chain primes into a list */
                Node *list = node_new(p, N_LIST);
                if (list) {
                    list->a = sup;
                    if (sup) sup->next = pr;
                    sup = list;
                }
            }
            continue;
        }
        break;
    }
    if (!sup && !sub) return nuc;
    {
        Node *n = node_new(p, N_SCRIPTS);
        if (!n) return nuc;
        n->a = nuc;
        n->b = sup;
        n->c = sub;
        return n;
    }
}

/* TeX-ish atom classes for automatic math spacing: 0 = ordinary, 1 = binary, 2 = relation. */
static int math_class(const Node *n) {
    if (!n) return 0;
    while (n->kind == N_SCRIPTS) n = n->a;
    if (n->kind != N_SYM) return 0;
    switch (n->ch) {
    case '=':
    case '<':
    case '>':
    case MG(MG_LEQ):
    case MG(MG_GEQ):
    case MG(MG_NEQ):
    case MG(MG_APPROX):
    case MG(MG_EQUIV):
        return 2;
    case '+':
    case '-':
    case '*':
    case MG(MG_PM):
    case MG(MG_CDOT):
    case MG(MG_TIMES):
    case MG(MG_DIV):
        return 1;
    default:
        return 0;
    }
}

static int math_is_ch(const Node *n, const char *set) {
    while (n && n->kind == N_SCRIPTS) n = n->a;
    return n && n->kind == N_SYM && n->ch < 128 && strchr(set, n->ch) != NULL;
}

static Node *parse_list(P *p, int stop) {
    Node *list = node_new(p, N_LIST);
    Node **tail;
    Node *prev = NULL;
    int prev_class = 0;
    if (!list) return NULL;
    tail = &list->a;
    for (;;) {
        Node *it;
        skip_sp(p);
        if (!*p->p) break;
        if (stop && *p->p == stop) break;
        if (*p->p == '$') break;
        if (*p->p == '&') {
            p->p++;
            continue;
        }
        it = parse_item(p);
        if (!it) break;
        {
            int ci = math_class(it);
            /* a leading sign is unary after an operator or an open bracket */
            if (ci == 1 && math_is_ch(it, "-") &&
                (prev == NULL || prev_class > 0 || math_is_ch(prev, "([")))
                ci = 0;
            /* medium space around binary operators, thick around relations */
            if (prev && (prev_class > 0) != (ci > 0) && !math_is_ch(it, ")].,;:")) {
                Node *sp = node_new(p, N_SPACE);
                if (sp) {
                    sp->space_em = (prev_class == 2 || ci == 2) ? 0.28f : 0.22f;
                    *tail = sp;
                    tail = &sp->next;
                }
            }
            prev_class = ci;
        }
        *tail = it;
        tail = &it->next;
        prev = it;
    }
    return list;
}

typedef struct {
    float w, h, d;
} Box;

static float glyph_w(int ch) {
    if (ch >= 128) return 0.72f;
    if (ch == ' ') return 0.35f;
    if (ch == '.' || ch == ',' || ch == '\'' || ch == ':') return 0.35f;
    if (ch == 'i' || ch == 'l' || ch == 'I' || ch == 'j' || ch == 'f' || ch == 't' || ch == '1')
        return 0.42f;
    if (ch == 'm' || ch == 'w' || ch == 'M' || ch == 'W') return 0.82f;
    if (ch == '(' || ch == ')' || ch == '[' || ch == ']') return 0.42f;
    return 0.62f;
}

static Box measure(Node *n);

static Box scale_box(Box b, float s) {
    b.w *= s;
    b.h *= s;
    b.d *= s;
    return b;
}

static Box measure(Node *n) {
    Box z = {0, 0, 0};
    if (!n) return z;
    switch (n->kind) {
    case N_SYM: {
        Box b;
        b.w = glyph_w(n->ch);
        b.h = 0.72f;
        b.d = 0.22f;
        if (n->ch == 'g' || n->ch == 'y' || n->ch == 'p' || n->ch == 'q' || n->ch == 'j' ||
            n->ch == MG(MG_MU) || n->ch == MG(MG_GAMMA) || n->ch == MG(MG_CHI))
            b.d = 0.32f;
        return b;
    }
    case N_SPACE: {
        Box b = z;
        b.w = n->space_em;
        return b;
    }
    case N_LIST: {
        Box b = z;
        Node *c;
        for (c = n->a; c; c = c->next) {
            Box u = measure(c);
            b.w += u.w;
            if (u.h > b.h) b.h = u.h;
            if (u.d > b.d) b.d = u.d;
        }
        if (b.w < 0) b.w = 0;
        return b;
    }
    case N_SCRIPTS: {
        Box nuc = measure(n->a);
        Box sup = n->b ? scale_box(measure(n->b), 0.7f) : z;
        Box sub = n->c ? scale_box(measure(n->c), 0.7f) : z;
        Box b;
        float extra = 0;
        if (sup.w > extra) extra = sup.w;
        if (sub.w > extra) extra = sub.w;
        b.w = nuc.w + extra * 0.95f;
        b.h = nuc.h;
        if (0.42f + sup.h > b.h) b.h = 0.42f + sup.h;
        b.d = nuc.d;
        if (0.22f + sub.d > b.d) b.d = 0.22f + sub.d;
        return b;
    }
    case N_FRAC: {
        float sc = n->display ? 1.0f : 0.72f;
        Box num = scale_box(measure(n->a), sc);
        Box den = scale_box(measure(n->b), sc);
        Box b;
        b.w = num.w > den.w ? num.w : den.w;
        b.w += 0.18f;
        b.h = num.h + num.d + 0.28f;
        b.d = den.h + den.d + 0.18f;
        return b;
    }
    case N_SQRT: {
        Box c = measure(n->a);
        Box b;
        b.w = c.w + 0.85f;
        b.h = c.h + 0.18f;
        b.d = c.d;
        if (n->c) {
            Box idx = scale_box(measure(n->c), 0.55f);
            if (idx.h + 0.2f > b.h) b.h = idx.h + 0.2f;
        }
        return b;
    }
    case N_OVER: {
        Box c = measure(n->a);
        c.h += 0.14f;
        return c;
    }
    }
    return z;
}

static void geom_push(FcTexGeom *g, int kind, float x, float y, float w, float h, int gid,
                      int italic, int bold, float size) {
    FcTexOp *o;
    if (g->n >= g->cap) {
        int ncap = g->cap ? g->cap * 2 : 32;
        FcTexOp *np = (FcTexOp *)realloc(g->ops, (size_t)ncap * sizeof(FcTexOp));
        if (!np) return;
        g->ops = np;
        g->cap = ncap;
    }
    o = &g->ops[g->n++];
    o->kind = kind;
    o->x = x;
    o->y = y;
    o->w = w;
    o->h = h;
    o->glyph = gid;
    o->italic = italic;
    o->bold = bold;
    o->size = size;
}

static void emit(FcTexGeom *g, Node *n, float x, float y, float size);

static void emit(FcTexGeom *g, Node *n, float x, float y, float size) {
    if (!n) return;
    switch (n->kind) {
    case N_SYM:
        geom_push(g, 0, x, y, glyph_w(n->ch) * size, size, n->ch, n->italic, n->bold, size);
        break;
    case N_SPACE:
        break;
    case N_LIST: {
        Node *c;
        for (c = n->a; c; c = c->next) {
            Box u = measure(c);
            emit(g, c, x, y, size);
            x += u.w * size;
        }
        break;
    }
    case N_SCRIPTS: {
        Box nuc = measure(n->a);
        emit(g, n->a, x, y, size);
        if (n->b)
            emit(g, n->b, x + nuc.w * size + 0.02f * size, y + 0.48f * size, size * 0.7f);
        if (n->c)
            emit(g, n->c, x + nuc.w * size + 0.02f * size, y - 0.22f * size, size * 0.7f);
        break;
    }
    case N_FRAC: {
        float sc = n->display ? 1.0f : 0.72f;
        Box num = measure(n->a), den = measure(n->b);
        Box B = measure(n);
        float nw = num.w * sc * size, dw = den.w * sc * size;
        float barw = B.w * size;
        float nx = x + (barw - nw) * 0.5f;
        float dx = x + (barw - dw) * 0.5f;
        float axis = y + 0.22f * size;
        emit(g, n->a, nx, axis + 0.12f * size + num.d * sc * size, size * sc);
        emit(g, n->b, dx, axis - 0.12f * size - den.h * sc * size, size * sc);
        geom_push(g, 1, x, axis, barw, 0.05f * size, 0, 0, 0, size);
        break;
    }
    case N_SQRT: {
        Box c = measure(n->a);
        float rad = 0.72f * size;
        emit(g, n->a, x + rad, y, size);
        /* vinculum */
        geom_push(g, 1, x + rad * 0.55f, y + c.h * size + 0.08f * size, c.w * size + 0.18f * size,
                  0.05f * size, 0, 0, 0, size);
        /* radical check */
        geom_push(g, 0, x, y, rad, size, MG(MG_SQRT), 0, 0, size);
        if (n->c) emit(g, n->c, x, y + c.h * size * 0.55f, size * 0.5f);
        break;
    }
    case N_OVER: {
        Box c = measure(n->a);
        emit(g, n->a, x, y, size);
        geom_push(g, 1, x, y + c.h * size + 0.02f * size, c.w * size, 0.05f * size, 0, 0, 0, size);
        break;
    }
    }
}

int fc_tex_compile(const char *latex, int display, FcTexGeom *g) {
    FcArena a;
    P p;
    Node *root;
    Box b;
    char *tmp;
    size_t n;
    memset(g, 0, sizeof *g);
    if (!latex) return 0;
    n = fc_strlen(latex);
    fc_arena_init(&a, 8192);
    tmp = fc_arena_strndup(&a, latex, n);
    p.p = tmp;
    p.arena = &a;
    p.err = 0;
    p.display = display;
    p.italic = 1;
    p.bold = 0;
    p.roman = 0;
    root = parse_list(&p, 0);
    if (!root || p.err) {
        fc_arena_free(&a);
        return 0;
    }
    b = measure(root);
    g->w = b.w;
    g->h = b.h;
    g->d = b.d;
    g->ok = 1;
    emit(g, root, 0, 0, 1);
    fc_arena_free(&a);
    return 1;
}

void fc_tex_geom_free(FcTexGeom *g) {
    free(g->ops);
    memset(g, 0, sizeof *g);
}

int fc_tex_measure(const char *latex, int display, float *w, float *h, float *d) {
    FcTexGeom g;
    int ok = fc_tex_compile(latex, display, &g);
    if (w) *w = g.w;
    if (h) *h = g.h;
    if (d) *d = g.d;
    fc_tex_geom_free(&g);
    return ok;
}
