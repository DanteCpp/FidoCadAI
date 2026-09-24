#include "fidocad.h"
#include "../src/internal.h" /* fc_xform_* */
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

static void test_stdlib_and_expand(void) {
    FcLib *lib = fc_lib_new();
    FcDrawing d, exp;
    int rc;
    EXPECT(lib != NULL);
    rc = fc_lib_load_stdlib(lib, "build/test_core");
    if (rc != FC_OK) rc = fc_lib_load_stdlib(lib, NULL);
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
    test_render_png();
    test_json_and_describe();
    test_tex();
    test_full_pattern_file();
    test_fmt_num_saturates();
    test_label_pair_roundtrip();
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
    printf("%d passed, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
