#include "fidocad.h"
#include "../src/internal.h" /* fc_xform_* */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail, g_ok;

#define EXPECT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            g_fail++;                                                                              \
        } else                                                                                     \
            g_ok++;                                                                                \
    } while (0)

static void test_parse_line(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d, "[FIDOCAD]\nLI 10 20 30 40 0\n", 0);
    EXPECT(d.nprims == 1);
    EXPECT(d.prims[0].kind == FC_LINE);
    EXPECT(d.prims[0].u.line.a.x == 10);
    EXPECT(d.prims[0].u.line.b.y == 40);
    EXPECT(d.prims[0].layer == 0);
    fc_drawing_free(&d);
}

static void test_te_upgrade(void) {
    FcDrawing d;
    char *out = NULL;
    FcEmitOpts o = {1, 1};
    fc_drawing_init(&d);
    fc_parse(&d, "TE 10 20 Hello world\n", 0);
    EXPECT(d.nprims == 1);
    EXPECT(d.prims[0].kind == FC_TEXT);
    EXPECT(d.prims[0].u.text.size_y == 4);
    EXPECT(strcmp(d.prims[0].u.text.text, "Hello world") == 0);
    fc_emit(&d, &out, &o);
    EXPECT(out && strstr(out, "TY 10 20 4 3 0 0 0 * Hello world"));
    free(out);
    fc_drawing_free(&d);
}

static void test_fcj_arrow(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d, "LI 5 50 90 50 0\nFCJ 2 0 3 1 0 0\n", 0);
    EXPECT(d.nprims == 1);
    EXPECT(d.prims[0].ext.arrows == 2);
    EXPECT(d.prims[0].ext.arrow_len == 3);
    EXPECT(d.prims[0].ext.dash == 0);
    fc_drawing_free(&d);
}

static void test_rect_dash_and_labels(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d,
             "RV 25 20 95 75 0\n"
             "FCJ 2 1\n"
             "TY 30 25 4 3 0 0 0 * BOX\n"
             "TY 30 30 4 3 0 0 0 * v1\n",
             0);
    EXPECT(d.nprims == 1);
    EXPECT(d.prims[0].kind == FC_RECT);
    EXPECT(d.prims[0].filled == 0);
    EXPECT(d.prims[0].ext.dash == 2);
    EXPECT(d.prims[0].has_name);
    EXPECT(d.prims[0].has_value);
    EXPECT(strcmp(d.prims[0].name.text, "BOX") == 0);
    EXPECT(strcmp(d.prims[0].value.text, "v1") == 0);
    fc_drawing_free(&d);
}

static void test_mc_labels(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d,
             "MC 100 100 0 0 080\n"
             "FCJ\n"
             "TY 110 105 4 3 0 0 0 * R1\n"
             "TY 110 110 4 3 0 0 0 * 10k\n"
             "LI 0 0 10 10 0\n",
             0);
    EXPECT(d.nprims == 2);
    EXPECT(d.prims[0].kind == FC_MACRO);
    EXPECT(strcmp(d.prims[0].u.macro.key, "080") == 0);
    EXPECT(d.prims[0].has_name);
    EXPECT(strcmp(d.prims[0].name.text, "R1") == 0);
    EXPECT(strcmp(d.prims[0].value.text, "10k") == 0);
    EXPECT(d.prims[1].kind == FC_LINE);
    fc_drawing_free(&d);
}

/* A TY right after SA/PL/PA/MC without an `FCJ` marker is independent text:
 * FidoCadJS ParserActions only starts label attachment on the FCJ line. */
static void test_ty_after_conn_is_text(void) {
    FcDrawing d;
    char *s;
    fc_drawing_init(&d);
    fc_parse(&d, "SA 30 210 7\nTY 60 210 5 3 0 0 0 * title\n", 0);
    EXPECT(d.nprims == 2);
    EXPECT(d.prims[0].kind == FC_CONN && !d.prims[0].has_name && !d.prims[0].has_value);
    EXPECT(d.prims[1].kind == FC_TEXT);
    EXPECT(strcmp(d.prims[1].u.text.text, "title") == 0);
    /* and a labeled connection still round-trips through the FCJ form */
    {
        FcDrawing e;
        fc_drawing_init(&e);
        fc_parse(&e, "SA 70 60 0\nFCJ\nTY 75 65 4 3 0 0 0 * N1\nTY 75 70 4 3 0 0 0 * net1\n", 0);
        EXPECT(e.nprims == 1);
        EXPECT(e.prims[0].has_name && e.prims[0].has_value);
        s = fc_prim_fcd(&e.prims[0]);
        EXPECT(s && strstr(s, "SA 70 60 0") && strstr(s, "FCJ\n") && strstr(s, "N1"));
        free(s);
        fc_drawing_free(&e);
    }
    /* a name-only label still emits both TY lines (FidoCadJS needs the pair) */
    {
        FcDrawing e;
        int tys = 0;
        char *q;
        fc_drawing_init(&e);
        fc_parse(&e, "SA 70 60 0\nFCJ\nTY 75 65 4 3 0 0 0 * N1\nTY 75 65 4 3 0 0 0 * \n", 0);
        EXPECT(e.nprims == 1 && e.prims[0].has_name && !e.prims[0].has_value);
        s = fc_prim_fcd(&e.prims[0]);
        if (s)
            for (q = s; (q = strstr(q, "TY ")); q += 3) tys++;
        EXPECT(tys == 2);
        free(s);
        fc_drawing_free(&e);
    }
    fc_drawing_free(&d);
}

static void test_standalone_ty_after_line(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d, "LI 10 10 20 20 0\nTY 60 210 5 3 0 0 0 * Hi\n", 0);
    EXPECT(d.nprims == 2);
    EXPECT(d.prims[0].kind == FC_LINE);
    EXPECT(d.prims[1].kind == FC_TEXT);
    EXPECT(strcmp(d.prims[1].u.text.text, "Hi") == 0);
    fc_drawing_free(&d);
}

static void test_clamp_and_skip(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d, "LI -5 20 30 40 0\nZZ 1 2 3\nLI 10 20 30 40 99\n", 0);
    EXPECT(d.nprims == 2);
    EXPECT(d.prims[0].u.line.a.x == 0);
    EXPECT(d.prims[1].layer == 0);
    fc_drawing_free(&d);
}

static void test_roundtrip(void) {
    const char *src =
        "[FIDOCAD]\n"
        "FJC C 1.5\n"
        "LI 25 45 110 25 0\n"
        "LI 5 50 90 50 0\n"
        "FCJ 2 0 3 1 0 0\n"
        "RV 25 20 95 75 0\n"
        "FCJ 2 1\n"
        "TY 30 25 4 3 0 0 0 * BOX\n"
        "TY 30 30 4 3 0 0 0 * v1\n"
        "EP 10 10 40 40 2\n"
        "PL 10 110 90 110 5 0\n"
        "PA 50 40 10 10 5 0 0\n"
        "SA 70 60 0\n"
        "CV 0 240 5 260 25 240 30 0\n"
        "TY 220 10 4 3 0 4 0 * Title\n";
    FcDrawing d;
    char *rep = NULL;
    fc_drawing_init(&d);
    fc_parse(&d, src, 0);
    EXPECT(d.nprims == 9);
    EXPECT(d.cfg.diameter_conn == 1.5);
    EXPECT(fc_validate(&d, &rep) == FC_OK);
    EXPECT(rep && strstr(rep, "round-trip ok"));
    free(rep);
    fc_drawing_free(&d);
}

static void test_poly(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d, "PV 10 20 30 40 50 60 0\nPP 1 1 2 2 3 3 1\n", 0);
    EXPECT(d.nprims == 2);
    EXPECT(d.prims[0].kind == FC_POLY);
    EXPECT(d.prims[0].filled == 0);
    EXPECT(d.prims[0].u.poly.n == 3);
    EXPECT(d.prims[1].filled == 1);
    EXPECT(d.prims[1].layer == 1);
    fc_drawing_free(&d);
}

/* PV/PP/CV without a trailing layer (as in the stdlib op-amp): every pair is
 * a vertex; only an odd leftover token is the layer. */
static void test_poly_no_layer(void) {
    FcDrawing d;
    char *a = NULL, *b = NULL;
    fc_drawing_init(&d);
    EXPECT(fc_parse_file(&d, "tests/fixtures/poly-no-layer.fcd") == FC_OK);
    EXPECT(d.nprims == 3);
    EXPECT(d.prims[0].u.poly.n == 3 && d.prims[0].layer == 0);
    EXPECT(d.prims[0].u.poly.pts[2].x == 120 && d.prims[0].u.poly.pts[2].y == 105);
    EXPECT(d.prims[1].u.poly.n == 4 && d.prims[1].layer == 3);
    EXPECT(d.prims[2].u.curve.n == 3 && d.prims[2].u.curve.closed);
    EXPECT(fc_emit(&d, &a, NULL) > 0);
    fc_drawing_free(&d);
    fc_drawing_init(&d);
    fc_parse(&d, a, 0);
    EXPECT(fc_emit(&d, &b, NULL) > 0);
    EXPECT(a && b && strcmp(a, b) == 0);
    EXPECT(d.prims[0].u.poly.n == 3);
    free(a);
    free(b);
    fc_drawing_free(&d);
}

static void test_fjc_layer(void) {
    FcDrawing d;
    fc_drawing_init(&d);
    fc_parse(&d, "FJC L 2 16711680 0.5\nFJC N 2 Top Copper\nLI 0 0 1 1 2\n", 0);
    EXPECT(d.layers[2].modified);
    EXPECT(d.layers[2].rgb == 16711680u);
    EXPECT(d.layers[2].alpha > 0.49f && d.layers[2].alpha < 0.51f);
    EXPECT(strcmp(d.layers[2].name, "Top Copper") == 0);
    fc_drawing_free(&d);
}

static void test_fmt_num(void) {
    char b[32];
    fc_fmt_num(b, sizeof b, 13);
    EXPECT(strcmp(b, "13") == 0);
    fc_fmt_num(b, sizeof b, 12.5);
    EXPECT(strcmp(b, "12.5") == 0);
    fc_fmt_num(b, sizeof b, 12.125);
    EXPECT(strcmp(b, "12.125") == 0);
    fc_fmt_num(b, sizeof b, 0.35);
    EXPECT(strcmp(b, "0.35") == 0);
}

typedef struct {
    int warnings, errors;
} Reports;

static void count_report(void *ctx, int error, const char *msg) {
    Reports *r = (Reports *)ctx;
    (void)msg;
    if (error) r->errors++;
    else r->warnings++;
}

/* fcai and fcai-gui share one search order and one failure policy: a missing
 * directory only warns, an unreadable library file fails the load. */
static void test_lib_load_all(void) {
    const char *dirs[] = {"tests/no-such-dir"};
    const char *files[] = {"tests/no-such-lib.fcl"};
    Reports r = {0, 0};
    FcLib *lib = fc_lib_new();
    EXPECT(fc_lib_load_all(lib, 1, dirs, 1, NULL, NULL, 0, count_report, &r) == FC_OK);
    EXPECT(r.warnings == 1 && r.errors == 0);
    EXPECT(fc_lib_find(lib, "080") != NULL);
    EXPECT(fc_lib_load_all(lib, 0, NULL, 0, files, NULL, 1, count_report, &r) == FC_ERR_IO);
    EXPECT(r.errors == 1);
    fc_lib_free(lib);
}

static void test_stdlib_and_expand(void) {
    FcLib *lib = fc_lib_new();
    FcDrawing d, exp;
    int rc;
    EXPECT(lib != NULL);
    rc = fc_lib_load_stdlib(lib);
    EXPECT(rc == FC_OK);
    EXPECT(fc_lib_find(lib, "080") != NULL);
    EXPECT(fc_lib_find(lib, "220") != NULL);
    EXPECT(fc_lib_count(lib) > 100);
    fc_drawing_init(&d);
    fc_parse(&d,
             "[FIDOCAD]\n"
             "MC 50 50 0 0 080\n"
             "TY 55 55 4 3 0 0 0 * R1\n"
             "TY 55 60 4 3 0 0 0 * 330\n",
             0);
    fc_drawing_init(&exp);
    EXPECT(fc_expand(&d, &exp, lib) == FC_OK);
    EXPECT(exp.nprims > 2); /* resistor body + 2 labels */
    {
        int i, nli = 0, nrv = 0, nty = 0;
        for (i = 0; i < exp.nprims; i++) {
            if (exp.prims[i].kind == FC_LINE) nli++;
            if (exp.prims[i].kind == FC_RECT) nrv++;
            if (exp.prims[i].kind == FC_TEXT) nty++;
        }
        EXPECT(nli >= 2);
        EXPECT(nrv >= 1);
        EXPECT(nty == 2);
    }
    fc_drawing_free(&d);
    fc_drawing_free(&exp);
    fc_lib_free(lib);
}

static void test_render_png(void) {
    FcDrawing d;
    FcRenderOpts o;
    const char *path = "build/test_render.png";
    FILE *fp;
    unsigned char sig[8];
    fc_drawing_init(&d);
    fc_parse(&d, "LI 10 10 90 40 0\nRV 20 20 60 50 0\nTY 25 25 5 3 0 0 0 * Hi\n", 0);
    fc_render_opts_init(&o);
    o.scale = 4;
    o.max_w = 400;
    o.max_h = 300;
    EXPECT(fc_render_file(&d, NULL, &o, path) == FC_OK);
    fp = fopen(path, "rb");
    EXPECT(fp != NULL);
    if (fp) {
        EXPECT(fread(sig, 1, 8, fp) == 8);
        EXPECT(sig[0] == 137 && sig[1] == 80 && sig[2] == 78 && sig[3] == 71);
        fclose(fp);
    }
    fc_drawing_free(&d);
}

static void test_json_and_describe(void) {
    FcDrawing d;
    char *js, *ds;
    fc_drawing_init(&d);
    fc_parse(&d, "MC 10 10 0 0 080\nTY 12 12 4 3 0 0 0 * R1\nLI 0 0 5 5 0\n", 0);
    js = fc_to_json(&d, NULL);
    ds = fc_describe(&d, NULL, NULL);
    EXPECT(js && strstr(js, "\"kind\":\"macro\""));
    EXPECT(js && strstr(js, "\"key\":\"080\""));
    EXPECT(ds && strstr(ds, "MC"));
    free(js);
    free(ds);
    fc_drawing_free(&d);
}

static void test_tex(void) {
    float w = 0, h = 0, d = 0;
    FcDrawing dr;
    FcRenderOpts o;
    FILE *fp;
    unsigned char sig[8];
    EXPECT(fc_tex_has_math("foo $R_1$ bar"));
    EXPECT(!fc_tex_has_math("hello"));
    EXPECT(fc_tex_measure("R_1", 0, &w, &h, &d) == 1);
    EXPECT(w > 0.7f);
    EXPECT(h > 0.5f);
    w = h = 0;
    EXPECT(fc_tex_measure("\\frac{1}{2}", 0, &w, &h, &d) == 1);
    EXPECT(h > 0.9f);
    w = 0;
    EXPECT(fc_tex_measure("\\Omega", 0, &w, &h, &d) == 1);
    EXPECT(w > 0.4f);
    w = h = d = 0;
    EXPECT(fc_tex_measure("a+b", 0, &w, &h, &d) == 1);
    EXPECT(w > 2.0f); /* binary operators get TeX-style medium spaces */
    w = h = d = 0;
    EXPECT(fc_tex_measure("-x", 0, &w, &h, &d) == 1);
    EXPECT(w < 1.5f); /* a leading sign stays unary, no space */
    fc_drawing_init(&dr);
    fc_parse(&dr,
             "[FIDOCAD]\n"
             "TY 10 10 8 5 0 0 0 * $R_1=\\frac{V}{I}$\n"
             "TY 10 30 8 5 0 0 0 * $10\\,\\mathrm{k}\\Omega$\n",
             0);
    EXPECT(dr.nprims == 2);
    EXPECT(fc_tex_has_math(dr.prims[0].u.text.text));
    fc_render_opts_init(&o);
    o.scale = 6;
    EXPECT(fc_render_file(&dr, NULL, &o, "build/test_tex.png") == FC_OK);
    fp = fopen("build/test_tex.png", "rb");
    EXPECT(fp != NULL);
    if (fp) {
        EXPECT(fread(sig, 1, 8, fp) == 8);
        EXPECT(sig[0] == 137 && sig[1] == 80);
        fclose(fp);
    }
    fc_drawing_free(&dr);
}

static int count_kind(const FcDrawing *d, FcKind k) {
    int i, n = 0;
    for (i = 0; i < d->nprims; i++)
        if (d->prims[i].kind == k) n++;
    return n;
}

static void test_fmt_num_saturates(void) {
    char b[64];
    FcDrawing d;
    char *out = NULL;
    fc_fmt_num(b, sizeof b, 1e30);
    EXPECT(strcmp(b, "1000000000000000") == 0);
    fc_fmt_num(b, sizeof b, -0.0001);
    EXPECT(strcmp(b, "0") == 0);
    fc_drawing_init(&d);
    fc_parse(&d, "PL 0 0 10 10 999999999999999999999999 0\nPA 5 5 -99999999999 3 0 0 0\n", 0);
    EXPECT(d.prims[0].u.pcb_line.width == FC_MAX_COORD);
    EXPECT(d.prims[1].u.pad.rx == -FC_MAX_COORD);
    fc_emit(&d, &out, NULL);
    EXPECT(out && strstr(out, "PL 0 0 10 10 1000000 0\n"));
    free(out);
    fc_drawing_free(&d);
}

static void test_label_pair_roundtrip(void) {
    FcDrawing d;
    char *rep = NULL;
    fc_drawing_init(&d);
    fc_parse(&d,
             "LI 0 0 10 10 0\nFCJ 0 1\nTY 5 5 4 3 0 0 0 * name\nFCJ\n"
             "TY 50 50 4 3 0 0 0 * standalone\n",
             0);
    EXPECT(d.nprims == 2);
    EXPECT(d.prims[0].has_name && !d.prims[0].has_value);
    EXPECT(fc_validate(&d, &rep) == FC_OK);
    free(rep);
    fc_drawing_free(&d);
}

/* FCJ stroke width: optional token between dash and text flag (FidoCadJS reads the
 * text flag as the last token, so it ignores the width and keeps the labels). */
static void test_fcj_stroke_width(void) {
    FcDrawing d, r;
    char *a, *b;
    fc_drawing_init(&d);
    fc_drawing_init(&r);
    EXPECT(fc_parse_file(&d, "tests/fixtures/stroke-width.fcd") == FC_OK);
    EXPECT(d.nprims == 9);
    EXPECT(d.ndiags == 0);
    EXPECT(d.prims[0].ext.width == 2 && d.prims[0].ext.dash == 0);
    EXPECT(fabs(d.prims[1].ext.width - 0.25) < 1e-9 && d.prims[1].ext.dash == 1 && d.prims[1].ext.arrows == 2);
    EXPECT(d.prims[1].has_name && d.prims[1].has_value);
    EXPECT(d.prims[2].kind == FC_RECT && d.prims[2].ext.width == 1.5);
    EXPECT(d.prims[3].kind == FC_OVAL && fabs(d.prims[3].ext.width - 0.1) < 1e-9 && d.prims[3].ext.dash == 2);
    EXPECT(d.prims[4].ext.width == 3 && fabs(d.prims[5].ext.width - 0.75) < 1e-9 && d.prims[6].ext.width == 1);
    EXPECT(d.prims[7].ext.width == 0 && d.prims[8].kind == FC_TEXT);
    a = NULL;
    fc_emit(&d, &a, NULL);
    EXPECT(a && strstr(a, "FCJ 2 0 3 1 1 0.25 1\n") && strstr(a, "FCJ 0 1.5 0\n"));
    EXPECT(a && !strstr(a, "FCJ 0 0\n")); /* no override, no FCJ */
    EXPECT(a && fc_parse(&r, a, 0) == FC_OK);
    b = NULL;
    fc_emit(&r, &b, NULL);
    EXPECT(a && b && strcmp(a, b) == 0);
    free(a);
    free(b);
    fc_drawing_free(&r);
    fc_drawing_init(&r);
    fc_parse(&r, "LI 0 0 10 0 0\nFCJ 0 -1 0\n", 0);
    EXPECT(r.nprims == 1 && r.prims[0].ext.width == 0 && r.ndiags == 1);
    fc_drawing_free(&r);
    fc_drawing_free(&d);
}

static void test_validate_degenerate(void) {
    FcDrawing d;
    char *rep = NULL;
    fc_drawing_init(&d);
    fc_parse(&d, "LI 5 5 5 5 0\nLI 0 0 10 10 0\n", 0);
    EXPECT(fc_validate(&d, &rep) == FC_OK);
    EXPECT(rep && strstr(rep, "1 degenerate primitive dropped"));
    free(rep);
    fc_drawing_free(&d);
}

static void test_recursive_macro(void) {
    FcLib *lib = fc_lib_new();
    FcDrawing d, exp;
    int i, nwarn = 0;
    fc_lib_load_text(lib,
                     "[FIDOLIB t]\n{c}\n[A a]\nMC 100 100 0 0 a\nMC 100 100 0 0 a\nMC 100 100 0 0 a\n"
                     "MC 100 100 0 0 a\nLI 100 100 110 100 0\n[B b]\nMC 100 100 0 0 a\n",
                     0, "");
    fc_drawing_init(&d);
    fc_drawing_init(&exp);
    fc_parse(&d, "MC 10 10 0 0 b\n", 0);
    EXPECT(fc_expand(&d, &exp, lib) == FC_OK);
    for (i = 0; i < exp.ndiags; i++)
        if (strstr(exp.diags[i].msg, "recursive macro 'a'")) nwarn++;
    EXPECT(nwarn == 1);
    EXPECT(count_kind(&exp, FC_LINE) == 1);
    fc_drawing_free(&d);
    fc_drawing_free(&exp);
    fc_lib_free(lib);
}

static void test_expand_budget(void) {
    /* 8 levels of 10x fan-out would be 10^8 lines */
    char text[4096];
    size_t n = 0;
    int lvl, k;
    FcLib *lib = fc_lib_new();
    FcDrawing d, exp;
    n += (size_t)snprintf(text + n, sizeof text - n, "[FIDOLIB t]\n{c}\n[M0 m0]\nLI 100 100 101 100 0\n");
    for (lvl = 1; lvl <= 8; lvl++) {
        n += (size_t)snprintf(text + n, sizeof text - n, "[M%d m]\n", lvl);
        for (k = 0; k < 10; k++)
            n += (size_t)snprintf(text + n, sizeof text - n, "MC 100 100 0 0 m%d\n", lvl - 1);
    }
    fc_lib_load_text(lib, text, n, "");
    fc_drawing_init(&d);
    fc_drawing_init(&exp);
    fc_parse(&d, "MC 10 10 0 0 m8\n", 0);
    EXPECT(fc_expand(&d, &exp, lib) != FC_OK);
    EXPECT(exp.nprims <= FC_MAX_EXPANDED_PRIMS);
    EXPECT(exp.ndiags == 1 && exp.diags[0].level == FC_DIAG_ERR);
    fc_drawing_free(&d);
    fc_drawing_free(&exp);
    fc_lib_free(lib);
}

static void test_nested_mirror(void) {
    FcLib *lib = fc_lib_new();
    FcDrawing d, exp;
    int i;
    fc_lib_load_text(lib,
                     "[FIDOLIB t]\n{c}\n[IN in]\nTY 100 100 4 3 0 0 0 * T\n"
                     "LI 100 100 110 100 0\n[OUT out]\nMC 100 100 0 1 in\n",
                     0, "");
    fc_drawing_init(&d);
    fc_drawing_init(&exp);
    /* mirrored twice: text reads normally again */
    fc_parse(&d, "MC 50 50 0 1 out\n", 0);
    fc_expand(&d, &exp, lib);
    for (i = 0; i < exp.nprims; i++)
        if (exp.prims[i].kind == FC_TEXT) {
            EXPECT((exp.prims[i].u.text.style & FC_STYLE_MIRROR) == 0);
            EXPECT(exp.prims[i].u.text.rotation == 0);
        }
    fc_drawing_free(&exp);
    fc_drawing_free(&d);
    /* mirrored + orient 1: the text runs the same way as the line (+y, i.e. -90 deg) */
    fc_drawing_init(&d);
    fc_drawing_init(&exp);
    fc_parse(&d, "MC 50 50 1 1 in\n", 0);
    fc_expand(&d, &exp, lib);
    for (i = 0; i < exp.nprims; i++) {
        if (exp.prims[i].kind == FC_TEXT) {
            EXPECT(exp.prims[i].u.text.style & FC_STYLE_MIRROR);
            EXPECT(exp.prims[i].u.text.rotation == 270);
        }
        if (exp.prims[i].kind == FC_LINE) EXPECT(exp.prims[i].u.line.b.y == 60);
    }
    fc_drawing_free(&exp);
    fc_drawing_free(&d);
    fc_lib_free(lib);
}

static void test_macro_xform_compose(void) {
    /* (orient 1, mirror 1) placed under (orient 1, mirror 0) must map points the same
     * way as transforming step by step */
    FcPrim p;
    FcPoint q = {110, 100}, ref = {110, 100};
    memset(&p, 0, sizeof p);
    p.kind = FC_MACRO;
    p.u.macro.p.x = 100;
    p.u.macro.p.y = 100;
    p.u.macro.orient = 1;
    p.u.macro.mirror = 1;
    fc_xform_prim(&p, 1, 0, 100, 100);
    fc_xform_point(&ref, 1, 1, 100, 100);
    fc_xform_point(&ref, 1, 0, 100, 100);
    fc_xform_point(&q, p.u.macro.orient, p.u.macro.mirror, 100, 100);
    EXPECT(q.x == ref.x && q.y == ref.y);
}

static int read_ppm(const char *path, int *w, int *h, unsigned char px[3]) {
    FILE *fp = fopen(path, "rb");
    int ok;
    if (!fp) return 0;
    ok = fscanf(fp, "P6 %d %d 255", w, h) == 2 && fgetc(fp) != EOF && fread(px, 1, 3, fp) == 3;
    fclose(fp);
    return ok;
}

static void test_render_opts_zero(void) {
    FcDrawing d;
    FcRenderOpts o;
    int w = 0, h = 0;
    unsigned char px[3] = {1, 1, 1};
    fc_drawing_init(&d);
    fc_parse(&d, "LI 0 0 10 10 0\n", 0);
    fc_render_opts_init(&o);
    o.scale = 2;
    o.margin = 0;
    o.bg = 0x000000;
    o.ppm = 1;
    EXPECT(fc_render_file(&d, NULL, &o, "build/test_opts.ppm") == FC_OK);
    EXPECT(read_ppm("build/test_opts.ppm", &w, &h, px));
    EXPECT(w == 20 && h == 20);
    EXPECT(px[0] == 0 && px[1] == 0 && px[2] == 0);
    fc_drawing_free(&d);
}

static void test_render_huge(void) {
    FcDrawing d;
    FcRenderOpts o;
    int w = 0, h = 0;
    unsigned char px[3];
    fc_drawing_init(&d);
    fc_parse(&d,
             "EV 0 0 1000000 1000000 0\nPA 10 10 99999999999999 5 0 0 0\n"
             "PL 0 0 10 10 999999999 0\nTY 0 0 99999999 3 0 0 0 * X\n"
             "BE 10 150 30 130 70 170 90 150 0\nFCJ 3 1 3 1 0 0\n"
             "CV 0 10 20 30 40 50 20 0\nFCJ 3 0 3 1 0 0\nMC 60 60 0 0 missing\n",
             0);
    fc_render_opts_init(&o);
    o.ppm = 1;
    o.grid = 1;
    EXPECT(fc_render_file(&d, NULL, &o, "build/test_huge.ppm") == FC_OK);
    EXPECT(read_ppm("build/test_huge.ppm", &w, &h, px));
    /* auto scale fits the requested box instead of cropping at the raster limit */
    EXPECT(w <= 1200 && h <= 900);
    fc_drawing_free(&d);
}

static void test_many_macros_and_json(void) {
    FcLib *lib = fc_lib_new();
    FcDrawing d;
    char *js;
    char *text = (char *)malloc(5000 * 40);
    size_t n = 0;
    int i, found = 0;
    n += (size_t)sprintf(text, "[FIDOLIB t]\n{c}\n");
    for (i = 0; i < 5000; i++) n += (size_t)sprintf(text + n, "[K%d k]\nLI 0 0 1 1 0\n", i);
    fc_lib_load_text(lib, text, n, "p");
    free(text);
    EXPECT(fc_lib_count(lib) == 5000);
    for (i = 0; i < 5000; i++) {
        char key[32];
        snprintf(key, sizeof key, "p.k%d", i);
        if (fc_lib_find(lib, key)) found++;
    }
    EXPECT(found == 5000);
    fc_lib_free(lib);
    fc_drawing_init(&d);
    fc_parse(&d, "LI 0 0 1 1 0\nMC 50 50 0 0 nope\n", 0);
    js = fc_to_json(&d, NULL);
    EXPECT(js && strstr(js, "\"alpha\":1,"));
    EXPECT(js && strstr(js, "\"alpha\":0.95,"));
    /* unresolved macros are drawn as a placeholder box, so the bbox covers it */
    EXPECT(js && strstr(js, "\"max_x\":"));
    free(js);
    {
        FcRect bb;
        EXPECT(fc_bounds(&d, NULL, &bb) && bb.max_y >= 54);
    }
    fc_drawing_free(&d);
}

static void test_edge_fixture(void) {
    FcDrawing d;
    char *rep = NULL;
    fc_drawing_init(&d);
    EXPECT(fc_parse_file(&d, "tests/fixtures/edge-cases.fcd") == FC_OK);
    /* FidoCadJS ParserActions only attaches name/value TY lines to SA/PL/PA/MC
     * when an explicit FCJ marker introduced them; the trailing TY after MC is
     * a standalone text primitive. */
    EXPECT(d.nprims == 8);
    EXPECT(count_kind(&d, FC_TEXT) == 2);
    EXPECT(fc_validate(&d, &rep) == FC_OK);
    free(rep);
    fc_drawing_free(&d);
}

static void test_exe_path(void) {
    char exe[4096], dir[4096];
    size_t n;
    EXPECT(fc_exe_path(exe, sizeof exe));
    n = strlen(exe);
    EXPECT(exe[0] == '/');
    EXPECT(n > 10 && strcmp(exe + n - 10, "/test_core") == 0);
    fc_dirname(exe, dir, sizeof dir);
    EXPECT(strlen(dir) == n - 10);
    fc_dirname("fcai", dir, sizeof dir);
    EXPECT(strcmp(dir, ".") == 0);
    fc_dirname("/fcai", dir, sizeof dir);
    EXPECT(strcmp(dir, "/") == 0);
}

static void test_drawing_lint(void) {
    FcDrawing d;
    char *s;
    fc_drawing_init(&d);
    fc_parse(&d, "LI 0 0 10 0 0\nLI 10 0 10 10 0\nSA 10 0 0\n", 0);
    s = fc_lint(&d, NULL, 1);
    EXPECT(s && strstr(s, "corner-dot")); free(s);
    fc_drawing_clear(&d);
    fc_parse(&d, "LI 0 0 20 0 0\nLI 10 0 10 10 0\n", 0);
    s = fc_lint(&d, NULL, 1);
    EXPECT(s && strstr(s, "missing-junction-dot")); free(s);
    fc_append_fcd(&d, "SA 10 0 0\n", 0);
    s = fc_lint(&d, NULL, 1);
    EXPECT(s && strstr(s, "\"count\":0")); free(s);
    fc_append_fcd(&d, "LI 15 0 25 0 0\n", 0);
    s = fc_lint(&d, NULL, 1);
    EXPECT(s && strstr(s, "overlapping-wires")); free(s);
    fc_drawing_clear(&d);
    /* A crossing without an endpoint is not automatically a connection. */
    fc_parse(&d, "LI 0 10 20 10 0\nLI 10 0 10 20 0\nLI 15 10 15 20 1\n", 0);
    s = fc_lint(&d, NULL, 1);
    EXPECT(s && strstr(s, "\"count\":0")); free(s);
    fc_drawing_free(&d);
}

static void test_dash_scale_and_crop(void) {
    FcDrawing d;
    FcView v = {0};
    uint8_t pixels[240 * 80 * 3];
    int scale;
    fc_drawing_init(&d);
    fc_parse(&d, "LI 0 10 80 10 0\nFCJ 0 0 3 2 1 0\n", 0);
    v.bg = 0xFFFFFF;
    for (scale = 1; scale <= 3; scale++) {
        v.scale = scale;
        EXPECT(fc_render_rgb(&d, NULL, &v, pixels, 240, 80, 240 * 3) == FC_OK);
        EXPECT(pixels[(10 * scale * 240 + 2 * scale) * 3] == 0);
        EXPECT(pixels[(10 * scale * 240 + 7 * scale) * 3] == 255);
        EXPECT(pixels[(10 * scale * 240 + 12 * scale) * 3] == 0);
        /* Panning clips the line without restarting its dash pattern. */
        v.ox = -6 * scale;
        EXPECT(fc_render_rgb(&d, NULL, &v, pixels, 240, 80, 240 * 3) == FC_OK);
        EXPECT(pixels[(10 * scale * 240 + scale) * 3] == 255);
        EXPECT(pixels[(10 * scale * 240 + 6 * scale) * 3] == 0);
        v.ox = 0;
    }
    d.prims[0].ext.dash = 4;
    v.scale = 2;
    EXPECT(fc_render_rgb(&d, NULL, &v, pixels, 240, 80, 240 * 3) == FC_OK);
    EXPECT(pixels[(20 * 240 + 15 * 2) * 3] == 255);
    EXPECT(pixels[(20 * 240 + 18 * 2) * 3] == 0);
    {
        FcRenderOpts o;
        FILE *f;
        char header[80];
        fc_render_opts_init(&o);
        o.has_crop = 1; o.crop = (FcRect){0, 5, 20, 15};
        o.scale = 2; o.margin = 0; o.ppm = 1;
        EXPECT(fc_render_file(&d, NULL, &o, "build/test-crop.ppm") == FC_OK);
        f = fopen("build/test-crop.ppm", "rb");
        EXPECT(f != NULL);
        if (f) {
            size_t n = fread(header, 1, sizeof header - 1, f); header[n] = 0;
            EXPECT(strstr(header, "40 20") != NULL); fclose(f);
        }
        remove("build/test-crop.ppm");
        o.crop.max_x = o.crop.min_x;
        EXPECT(fc_render_file(&d, NULL, &o, "build/test-crop.ppm") == FC_ERR_ARG);
    }
    fc_drawing_free(&d);
}

/* Zooming in small steps must grow text steadily: no glyph snaps backwards
 * and the ink centroid never jumps (rounded advances used to move it ~9 px). */
static void test_text_zoom_smooth(void) {
    FcDrawing d;
    FcView v = {0};
    enum { W = 600, H = 120 };
    static uint8_t pixels[W * H * 3];
    double last = 0, last_step = 0, worst = 0;
    int prev_edge = -1, backwards = 0, i, x, y;
    fc_drawing_init(&d);
    fc_parse(&d, "TY 20 10 4 3 0 0 0 * Resistor R1 10k VCO\n", 0);
    v.bg = 0xFFFFFF;
    v.ox = 13.37;
    v.oy = 7.1;
    for (i = 0; i < 80; i++) {
        double sx = 0, sw = 0, cx;
        int edge = -1;
        v.scale = 3.0 * pow(1.002, i);
        EXPECT(fc_render_rgb(&d, NULL, &v, pixels, W, H, W * 3) == FC_OK);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int k = 255 - pixels[(y * W + x) * 3];
                if (k <= 8) continue;
                if (x > edge) edge = x;
                sx += (double)k * x;
                sw += k;
            }
        EXPECT(sw > 0);
        cx = sw > 0 ? sx / sw : 0;
        if (edge < prev_edge) backwards++;
        if (i > 1 && fabs((cx - last) - last_step) > worst) worst = fabs((cx - last) - last_step);
        if (i) last_step = cx - last;
        last = cx;
        prev_edge = edge;
    }
    EXPECT(backwards == 0);
    EXPECT(worst < 1.0);
    fc_drawing_free(&d);
}

/* Zooming out must shrink text with the drawing: same width/height ratio and
 * the same size relative to the geometry (no minimum glyph size in pixels). */
static void ink_extent(const uint8_t *px, int w, int h, int *iw, int *ih) {
    int x, y, x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (px[(y * w + x) * 3] < 160) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    *iw = x1 < 0 ? 0 : x1 - x0 + 1;
    *ih = y1 < 0 ? 0 : y1 - y0 + 1;
}

static void test_text_zoom_out_proportional(void) {
    FcDrawing d;
    FcView v = {0};
    enum { W = 800, H = 200 };
    static uint8_t pixels[W * H * 3];
    int w4, h4, w1, h1;
    fc_drawing_init(&d);
    fc_parse(&d, "TY 2 2 12 9 0 0 0 * HELLO WORLD HELLO\n", 0);
    v.bg = 0xFFFFFF;
    v.scale = 2;
    EXPECT(fc_render_rgb(&d, NULL, &v, pixels, W, H, W * 3) == FC_OK);
    ink_extent(pixels, W, H, &w4, &h4);
    v.scale = 0.5; /* 4.8 px tall: used to be held at 8 px */
    EXPECT(fc_render_rgb(&d, NULL, &v, pixels, W, H, W * 3) == FC_OK);
    ink_extent(pixels, W, H, &w1, &h1);
    EXPECT(w4 > 0 && h4 > 0 && w1 > 0 && h1 > 0);
    /* 4x smaller, within antialiasing */
    EXPECT(fabs(w1 - w4 / 4.0) <= 3.0);
    EXPECT(fabs(h1 - h4 / 4.0) <= 2.0);
    fc_drawing_free(&d);
}

static void test_label_handles(void) {
    FcDrawing d, roundtrip;
    FcPoint point;
    char *emitted = NULL, *again = NULL;
    int index;
    fc_drawing_init(&d);
    fc_drawing_init(&roundtrip);
    fc_parse(&d,
             "LI 10 10 30 10 0\nBE 10 10 15 20 25 20 30 10 0\n"
             "RV 10 10 30 20 0\nEV 10 10 30 20 0\n"
             "PV 10 10 20 20 30 10 0\nCV 0 10 10 20 20 30 10 0\n"
             "SA 10 10 0\nPL 10 10 30 10 2 0\nPA 10 10 5 5 2 0 0\n"
             "MC 10 10 3 1 080\nTY 10 10 4 3 0 0 0 * text\n", 0);
    EXPECT(d.nprims == 11);
    for (index = 0; index < d.nprims - 1; index++) {
        FcPrim *p = &d.prims[index];
        int n = fc_prim_handles(p);
        char *before = fc_prim_fcd(p), *after;
        EXPECT(fc_prim_set_text(&d, index, 1, "R1") == FC_OK);
        EXPECT(fc_prim_set_text(&d, index, 2, "10k") == FC_OK);
        EXPECT(fc_prim_handles(p) == n + 2);
        fc_prim_set_handle(p, n, 40, 45);
        EXPECT(p->name.p.x == 40 && p->name.p.y == 45);
        EXPECT(p->value.p.x == 20 && p->value.p.y == 15);
        fc_prim_set_handle(p, n + 1, 60, 65);
        EXPECT(p->name.p.x == 40 && p->name.p.y == 45);
        EXPECT(p->value.p.x == 60 && p->value.p.y == 65);
        EXPECT(fc_prim_handle(p, n, &point) && point.x == 40 && point.y == 45);
        EXPECT(fc_prim_handle(p, n + 1, &point) && point.x == 60 && point.y == 65);
        EXPECT(!fc_prim_handle(p, -1, &point));
        EXPECT(!fc_prim_handle(p, n + 2, &point));
        fc_prim_set_handle(p, -1, 0, 0);
        fc_prim_set_handle(p, n + 2, 0, 0);
        EXPECT(p->name.p.x == 40 && p->value.p.x == 60);
        /* Removing the labels leaves precisely the original geometry/style. */
        p->has_name = p->has_value = 0;
        after = fc_prim_fcd(p);
        EXPECT(before && after && strcmp(before, after) == 0);
        free(before); free(after);
        p->has_name = p->has_value = 1;
    }
    EXPECT(fc_prim_handles(&d.prims[d.nprims - 1]) == 1); /* standalone TY */
    fc_emit(&d, &emitted, NULL);
    EXPECT(emitted && fc_parse(&roundtrip, emitted, 0) == FC_OK);
    fc_emit(&roundtrip, &again, NULL);
    EXPECT(emitted && again && strcmp(emitted, again) == 0);
    free(emitted); free(again);

    /* Empty FCJ slots stay hidden, including a value with no name. */
    fc_drawing_clear(&d);
    fc_parse(&d, "MC 100 100 0 0 080\nFCJ\n"
                 "TY 110 100 4 3 0 0 0 * \nTY 110 105 4 3 0 0 0 * 10k\n", 0);
    EXPECT(d.nprims == 1 && !d.prims[0].has_name && d.prims[0].has_value);
    EXPECT(fc_prim_handles(&d.prims[0]) == 2);
    EXPECT(fc_prim_handle(&d.prims[0], 1, &point) && point.x == 110 && point.y == 105);
    /* Direct API callers may keep a label slot whose text is empty. */
    d.prims[0].has_name = 1;
    d.prims[0].name.text = "";
    d.prims[0].name.p = (FcPoint){110, 100};
    EXPECT(fc_prim_handles(&d.prims[0]) == 2);
    fc_prim_set_handle(&d.prims[0], 1, -10, FC_MAX_COORD + 10);
    EXPECT(d.prims[0].value.p.x == 0 && d.prims[0].value.p.y == FC_MAX_COORD);
    EXPECT(d.prims[0].name.p.x == 110 && d.prims[0].name.p.y == 100);
    fc_prim_set_text(&d, 0, 2, "");
    EXPECT(fc_prim_handles(&d.prims[0]) == 1);
    fc_prim_set_text(&d, 0, 1, "R1");
    EXPECT(fc_prim_handles(&d.prims[0]) == 2);
    EXPECT(fc_prim_handle(&d.prims[0], 1, &point) && point.x == 110 && point.y == 100);
    EXPECT(fc_prim_handles(NULL) == 0);
    EXPECT(!fc_prim_handle(NULL, 0, &point));
    fc_prim_set_handle(NULL, 0, 0, 0);
    fc_drawing_free(&roundtrip);
    fc_drawing_free(&d);
}

static void test_full_pattern_file(void) {
    FcDrawing d;
    char *rep = NULL;
    fc_drawing_init(&d);
    EXPECT(fc_parse_file(&d, "tests/fixtures/full-pattern.fcd") == FC_OK);
    EXPECT(d.nprims >= 8);
    EXPECT(fc_validate(&d, &rep) == FC_OK);
    free(rep);
    fc_drawing_free(&d);
}

int main(int argc, char **argv) {
    (void)argc;
    fc_set_argv0(argv[0]);
    test_parse_line();
    test_te_upgrade();
    test_fcj_arrow();
    test_rect_dash_and_labels();
    test_mc_labels();
    test_ty_after_conn_is_text();
    test_standalone_ty_after_line();
    test_clamp_and_skip();
    test_roundtrip();
    test_poly();
    test_poly_no_layer();
    test_fjc_layer();
    test_fmt_num();
    test_stdlib_and_expand();
    test_lib_load_all();
    test_render_png();
    test_json_and_describe();
    test_tex();
    test_full_pattern_file();
    test_fmt_num_saturates();
    test_label_pair_roundtrip();
    test_fcj_stroke_width();
    test_validate_degenerate();
    test_recursive_macro();
    test_expand_budget();
    test_nested_mirror();
    test_macro_xform_compose();
    test_render_opts_zero();
    test_render_huge();
    test_many_macros_and_json();
    test_edge_fixture();
    test_exe_path();
    test_drawing_lint();
    test_dash_scale_and_crop();
    test_text_zoom_smooth();
    test_text_zoom_out_proportional();
    test_label_handles();
    printf("%d passed, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
