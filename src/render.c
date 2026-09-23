#include "internal.h"
#include "font8x8.h"
#include "stb_image_write.h"

#include <math.h>

typedef struct {
    uint8_t *px; /* RGB */
    int w, h;
    double ox, oy; /* origin in pixels of logical (0,0) */
    double scale;
    int dash_phase;
    int tex;
} Raster;

static void put_px(Raster *r, int x, int y, uint32_t rgb, float alpha) {
    uint8_t *p;
    int R, G, B;
    if ((unsigned)x >= (unsigned)r->w || (unsigned)y >= (unsigned)r->h) return;
    if (alpha <= 0) return;
    if (alpha > 1) alpha = 1;
    p = r->px + ((size_t)y * (size_t)r->w + (size_t)x) * 3;
    R = (int)((rgb >> 16) & 255);
    G = (int)((rgb >> 8) & 255);
    B = (int)(rgb & 255);
    p[0] = (uint8_t)(p[0] * (1 - alpha) + R * alpha);
    p[1] = (uint8_t)(p[1] * (1 - alpha) + G * alpha);
    p[2] = (uint8_t)(p[2] * (1 - alpha) + B * alpha);
}

static int mapx(const Raster *r, double x) { return (int)floor(x * r->scale + r->ox + 0.5); }
static int mapy(const Raster *r, double y) { return (int)floor(y * r->scale + r->oy + 0.5); }

static int dash_on(int style, int dist) {
    /* style 0 solid; 1 dashed; 2 fine dotted; 3 dotted; 4 dash-dot */
    int p;
    if (style <= 0) return 1;
    if (dist < 0) dist = -dist;
    switch (style) {
    case 1:
        p = dist % 10;
        return p < 5;
    case 2:
        p = dist % 4;
        return p < 2;
    case 3:
        p = dist % 7;
        return p < 2;
    case 4:
        p = dist % 14;
        return p < 2 || (p >= 7 && p < 12);
    default:
        return 1;
    }
}

static void plot(Raster *r, int x, int y, uint32_t rgb, float a, int dash, int dist) {
    if (dash_on(dash, dist)) put_px(r, x, y, rgb, a);
}

static void line_aa(Raster *r, int x0, int y0, int x1, int y1, uint32_t rgb, float a, int w,
                    int dash) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2, dist = 0;
    int thick = w < 1 ? 1 : w;
    for (;;) {
        int t;
        for (t = -(thick / 2); t <= thick / 2; t++) {
            if (dx > -dy)
                plot(r, x0, y0 + t, rgb, a, dash, dist);
            else
                plot(r, x0 + t, y0, rgb, a, dash, dist);
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
        dist++;
    }
}

static void fill_rect(Raster *r, int x0, int y0, int x1, int y1, uint32_t rgb, float a) {
    int x, y;
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= r->w) x1 = r->w - 1;
    if (y1 >= r->h) y1 = r->h - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) put_px(r, x, y, rgb, a);
}

static void stroke_rect(Raster *r, int x0, int y0, int x1, int y1, uint32_t rgb, float a, int w,
                        int dash) {
    line_aa(r, x0, y0, x1, y0, rgb, a, w, dash);
    line_aa(r, x1, y0, x1, y1, rgb, a, w, dash);
    line_aa(r, x1, y1, x0, y1, rgb, a, w, dash);
    line_aa(r, x0, y1, x0, y0, rgb, a, w, dash);
}

static void ellipse(Raster *r, int x0, int y0, int x1, int y1, uint32_t rgb, float a, int w, int dash,
                    int fill) {
    double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0;
    double rx = fabs((x1 - x0) / 2.0), ry = fabs((y1 - y0) / 2.0);
    int xi, yi;
    int minx, maxx, miny, maxy;
    if (rx < 0.5) rx = 0.5;
    if (ry < 0.5) ry = 0.5;
    minx = (int)floor(cx - rx) - w;
    maxx = (int)ceil(cx + rx) + w;
    miny = (int)floor(cy - ry) - w;
    maxy = (int)ceil(cy + ry) + w;
    for (yi = miny; yi <= maxy; yi++) {
        for (xi = minx; xi <= maxx; xi++) {
            double dx = (xi - cx) / rx, dy = (yi - cy) / ry;
            double e = dx * dx + dy * dy;
            if (fill) {
                if (e <= 1.0) put_px(r, xi, yi, rgb, a);
            } else {
                double inner = 1.0 - (w + 0.6) / (rx < ry ? rx : ry);
                if (inner < 0.2) inner = 0.2;
                if (e <= 1.05 && e >= inner * inner) {
                    int dist = (int)(atan2(dy, dx) * (rx + ry));
                    plot(r, xi, yi, rgb, a, dash, dist);
                }
            }
        }
    }
}

static void fill_circle(Raster *r, int cx, int cy, int rad, uint32_t rgb, float a) {
    int x, y;
    int r2 = rad * rad;
    for (y = -rad; y <= rad; y++)
        for (x = -rad; x <= rad; x++)
            if (x * x + y * y <= r2) put_px(r, cx + x, cy + y, rgb, a);
}

static void cubic(FcPoint *o, FcPoint a, FcPoint b, FcPoint c, FcPoint d, double t) {
    double u = 1 - t;
    double uu = u * u, tt = t * t;
    o->x = uu * u * a.x + 3 * uu * t * b.x + 3 * u * tt * c.x + tt * t * d.x;
    o->y = uu * u * a.y + 3 * uu * t * b.y + 3 * u * tt * c.y + tt * t * d.y;
}

static void bezier(Raster *r, FcPoint p0, FcPoint p1, FcPoint p2, FcPoint p3, uint32_t rgb, float a,
                   int w, int dash) {
    FcPoint prev = p0;
    int i, n = 32;
    for (i = 1; i <= n; i++) {
        FcPoint cur;
        cubic(&cur, p0, p1, p2, p3, (double)i / n);
        line_aa(r, mapx(r, prev.x), mapy(r, prev.y), mapx(r, cur.x), mapy(r, cur.y), rgb, a, w, dash);
        prev = cur;
    }
}

static void fill_poly(Raster *r, const FcPoint *pts, int n, uint32_t rgb, float a) {
    int *ys, i, y, miny, maxy;
    if (n < 3) return;
    miny = maxy = mapy(r, pts[0].y);
    for (i = 1; i < n; i++) {
        int yy = mapy(r, pts[i].y);
        if (yy < miny) miny = yy;
        if (yy > maxy) maxy = yy;
    }
    if (miny < 0) miny = 0;
    if (maxy >= r->h) maxy = r->h - 1;
    ys = (int *)malloc((size_t)n * sizeof(int));
    if (!ys) return;
    for (y = miny; y <= maxy; y++) {
        int ne = 0, j, k, x;
        for (i = 0, j = n - 1; i < n; j = i++) {
            int y0 = mapy(r, pts[j].y), y1 = mapy(r, pts[i].y);
            int x0 = mapx(r, pts[j].x), x1 = mapx(r, pts[i].x);
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                int den = y1 - y0;
                int xx = x0 + (int)((long)(x1 - x0) * (y - y0) / (den ? den : 1));
                ys[ne++] = xx;
            }
        }
        for (i = 0; i < ne; i++)
            for (j = i + 1; j < ne; j++)
                if (ys[j] < ys[i]) {
                    k = ys[i];
                    ys[i] = ys[j];
                    ys[j] = k;
                }
        for (i = 0; i + 1 < ne; i += 2) {
            int xa = ys[i], xb = ys[i + 1];
            if (xa < 0) xa = 0;
            if (xb >= r->w) xb = r->w - 1;
            for (x = xa; x <= xb; x++) put_px(r, x, y, rgb, a);
        }
    }
    free(ys);
}

static void stroke_poly(Raster *r, const FcPoint *pts, int n, int closed, uint32_t rgb, float a,
                        int w, int dash) {
    int i;
    if (n < 2) return;
    for (i = 0; i < n - 1; i++)
        line_aa(r, mapx(r, pts[i].x), mapy(r, pts[i].y), mapx(r, pts[i + 1].x), mapy(r, pts[i + 1].y),
                rgb, a, w, dash);
    if (closed)
        line_aa(r, mapx(r, pts[n - 1].x), mapy(r, pts[n - 1].y), mapx(r, pts[0].x), mapy(r, pts[0].y),
                rgb, a, w, dash);
}

/* Catmull-Rom through points, converted to cubics. */
static void spline(Raster *r, const FcPoint *pts, int n, int closed, uint32_t rgb, float a, int w,
                   int dash) {
    int i, m = n;
    if (n < 2) return;
    if (n == 2) {
        stroke_poly(r, pts, n, 0, rgb, a, w, dash);
        return;
    }
    for (i = 0; i < (closed ? n : n - 1); i++) {
        FcPoint p0, p1, p2, p3, c1, c2;
        int i0 = closed ? (i - 1 + m) % m : (i == 0 ? 0 : i - 1);
        int i1 = i;
        int i2 = closed ? (i + 1) % m : (i + 1 < n ? i + 1 : n - 1);
        int i3 = closed ? (i + 2) % m : (i + 2 < n ? i + 2 : n - 1);
        p0 = pts[i0];
        p1 = pts[i1];
        p2 = pts[i2];
        p3 = pts[i3];
        c1.x = p1.x + (p2.x - p0.x) / 6.0;
        c1.y = p1.y + (p2.y - p0.y) / 6.0;
        c2.x = p2.x - (p3.x - p1.x) / 6.0;
        c2.y = p2.y - (p3.y - p1.y) / 6.0;
        bezier(r, p1, c1, c2, p2, rgb, a, w, dash);
    }
}

static void arrow_head(Raster *r, FcPoint from, FcPoint to, double len, double half, uint32_t rgb,
                       float a, int empty) {
    double dx = to.x - from.x, dy = to.y - from.y;
    double L = sqrt(dx * dx + dy * dy);
    double ux, uy, px, py;
    FcPoint t, l, rr;
    if (L < 1e-6) return;
    ux = dx / L;
    uy = dy / L;
    px = -uy;
    py = ux;
    t = to;
    l.x = to.x - ux * len + px * half;
    l.y = to.y - uy * len + py * half;
    rr.x = to.x - ux * len - px * half;
    rr.y = to.y - uy * len - py * half;
    if (empty) {
        line_aa(r, mapx(r, t.x), mapy(r, t.y), mapx(r, l.x), mapy(r, l.y), rgb, a, 1, 0);
        line_aa(r, mapx(r, t.x), mapy(r, t.y), mapx(r, rr.x), mapy(r, rr.y), rgb, a, 1, 0);
    } else {
        FcPoint tri[3] = {t, l, rr};
        fill_poly(r, tri, 3, rgb, a);
    }
}

static void stroke_seg(Raster *r, int x0, int y0, double ca, double sa, int mirror, float x1,
                       float y1, float x2, float y2, int thick, uint32_t rgb, float a) {
    int ax, ay, bx, by;
    if (mirror) {
        x1 = -x1;
        x2 = -x2;
    }
    ax = x0 + (int)floor(x1 * ca - y1 * sa + 0.5);
    ay = y0 + (int)floor(x1 * sa + y1 * ca + 0.5);
    bx = x0 + (int)floor(x2 * ca - y2 * sa + 0.5);
    by = y0 + (int)floor(x2 * sa + y2 * ca + 0.5);
    line_aa(r, ax, ay, bx, by, rgb, a, thick < 1 ? 1 : thick, 0);
}

static void map_font(float fx, float fy, float ox, float sx, float sy, int italic, float *gx,
                     float *gy) {
    if (italic) fx += 0.22f * fy;
    *gx = ox + fx * sx;
    *gy = (12.0f - fy) * sy; /* cap height at top of text box */
}

static void draw_accent(Raster *r, int acc, float cx, float top, float ox, float sx, float sy,
                        int italic, int x0, int y0, double ca, double sa, int mirror, int thick,
                        uint32_t rgb, float a) {
    float gx1, gy1, gx2, gy2, gx3, gy3;
    if (!acc) return;
    if (acc == 1) { /* acute */
        map_font(cx - 0.5f, top + 1.5f, ox, sx, sy, italic, &gx1, &gy1);
        map_font(cx + 1.8f, top + 3.2f, ox, sx, sy, italic, &gx2, &gy2);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx1, gy1, gx2, gy2, thick, rgb, a);
    } else if (acc == 2) { /* grave */
        map_font(cx + 1.8f, top + 1.5f, ox, sx, sy, italic, &gx1, &gy1);
        map_font(cx - 0.5f, top + 3.2f, ox, sx, sy, italic, &gx2, &gy2);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx1, gy1, gx2, gy2, thick, rgb, a);
    } else if (acc == 3) { /* circ */
        map_font(cx - 1.4f, top + 1.6f, ox, sx, sy, italic, &gx1, &gy1);
        map_font(cx, top + 3.2f, ox, sx, sy, italic, &gx2, &gy2);
        map_font(cx + 1.4f, top + 1.6f, ox, sx, sy, italic, &gx3, &gy3);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx1, gy1, gx2, gy2, thick, rgb, a);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx2, gy2, gx3, gy3, thick, rgb, a);
    } else if (acc == 4) { /* umlaut */
        map_font(cx - 1.2f, top + 2.6f, ox, sx, sy, italic, &gx1, &gy1);
        map_font(cx - 0.4f, top + 2.6f, ox, sx, sy, italic, &gx2, &gy2);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx1, gy1, gx2, gy2, thick, rgb, a);
        map_font(cx + 0.8f, top + 2.6f, ox, sx, sy, italic, &gx1, &gy1);
        map_font(cx + 1.6f, top + 2.6f, ox, sx, sy, italic, &gx2, &gy2);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx1, gy1, gx2, gy2, thick, rgb, a);
    } else if (acc == 5) { /* tilde */
        map_font(cx - 1.8f, top + 2.0f, ox, sx, sy, italic, &gx1, &gy1);
        map_font(cx - 0.4f, top + 3.0f, ox, sx, sy, italic, &gx2, &gy2);
        map_font(cx + 0.4f, top + 1.6f, ox, sx, sy, italic, &gx3, &gy3);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx1, gy1, gx2, gy2, thick, rgb, a);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx2, gy2, gx3, gy3, thick, rgb, a);
        map_font(cx + 1.8f, top + 2.6f, ox, sx, sy, italic, &gx1, &gy1);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx3, gy3, gx1, gy1, thick, rgb, a);
    } else if (acc == 6) { /* cedilla */
        map_font(cx, 0, ox, sx, sy, italic, &gx1, &gy1);
        map_font(cx, -2.2f, ox, sx, sy, italic, &gx2, &gy2);
        map_font(cx + 1.2f, -3.0f, ox, sx, sy, italic, &gx3, &gy3);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx1, gy1, gx2, gy2, thick, rgb, a);
        stroke_seg(r, x0, y0, ca, sa, mirror, gx2, gy2, gx3, gy3, thick, rgb, a);
    }
}

static float draw_cp_stroke(Raster *r, int cp, float pen, float sx, float sy, int mono, int italic,
                            int bold, int x0, int y0, double ca, double sa, int mirror, uint32_t rgb,
                            float a) {
    int8_t xy[256];
    int n, i, l, rgt, thick, acc;
    float adv, xoff, px0, py0, px1, py1;
    int have = 0;
    if (cp == 10 || cp == 13 || cp == 0) return 0;
    n = fc_stroke_points(cp, xy, 256);
    fc_stroke_lr(cp, &l, &rgt);
    adv = mono ? 10.0f : (float)(rgt - l);
    xoff = mono ? (10.0f - (float)(rgt - l)) * 0.5f - (float)l : -(float)l;
    thick = 1;
    if (bold && sy >= 1.2f) thick = 2;
    if (sy * 12.0f >= 22.0f && !bold) thick = 2;
    if (bold && sy * 12.0f >= 22.0f) thick = 3;
    for (i = 0; i + 1 < n;) {
        unsigned char ax = (unsigned char)xy[i], ay = (unsigned char)xy[i + 1];
        if (ax == 127 || ay == 127) {
            have = 0;
            i += 2;
            continue;
        }
        map_font((float)xy[i] + xoff, (float)xy[i + 1], pen, sx, sy, italic, &px1, &py1);
        if (have) stroke_seg(r, x0, y0, ca, sa, mirror, px0, py0, px1, py1, thick, rgb, a);
        px0 = px1;
        py0 = py1;
        have = 1;
        i += 2;
    }
    acc = fc_stroke_accent(cp);
    if (acc) {
        int base, ac2;
        float cx = xoff + (float)l + (float)(rgt - l) * 0.5f;
        float top = 12.0f;
        fc_font_decompose(cp, &base, &ac2);
        if (base >= 'a' && base <= 'z') top = 8.0f;
        draw_accent(r, acc, cx, top, pen, sx, sy, italic, x0, y0, ca, sa, mirror, thick, rgb, a);
    }
    return adv * sx;
}

static void blit_rule(Raster *r, int x0, int y0, double ca, double sa, int mirror, float px,
                      float py, float w, float h, uint32_t rgb, float a) {
    int ix, iy;
    int rw = (int)ceil(w), rh = (int)ceil(h);
    if (rw < 1) rw = 1;
    if (rh < 1) rh = 1;
    for (iy = 0; iy < rh; iy++) {
        for (ix = 0; ix < rw; ix++) {
            float gx = px + (float)ix, gy = py + (float)iy;
            int qx, qy;
            if (mirror) gx = -gx;
            qx = x0 + (int)floor(gx * ca - gy * sa + 0.5);
            qy = y0 + (int)floor(gx * sa + gy * ca + 0.5);
            put_px(r, qx, qy, rgb, a);
        }
    }
}

static void blit_bitmap_glyph(Raster *r, int x0, int y0, double ca, double sa, int mirror, int gid,
                              float px, float py, float gw, float gh, int italic, int bold,
                              uint32_t rgb, float a) {
    const unsigned char *g = fc_tex_glyph_bits(gid);
    int row, col, ix, iy;
    int cw = (int)(gw / 8.0f) + 1;
    int ch = (int)(gh / 8.0f) + 1;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            if (!((g[row] >> (7 - col)) & 1)) continue;
            int gx0 = (int)(col * gw / 8.0f);
            int gy0 = (int)(row * gh / 8.0f);
            for (iy = 0; iy < ch; iy++) {
                for (ix = 0; ix < cw; ix++) {
                    float gx = px + (float)gx0 + (float)ix;
                    float gy = py + (float)gy0 + (float)iy;
                    int qx, qy;
                    if (italic) gx += 0.22f * (gh * 0.75f - ((float)gy0 + (float)iy));
                    if (mirror) gx = -gx;
                    qx = x0 + (int)floor(gx * ca - gy * sa + 0.5);
                    qy = y0 + (int)floor(gx * sa + gy * ca + 0.5);
                    put_px(r, qx, qy, rgb, a);
                    if (bold) put_px(r, qx + 1, qy, rgb, a);
                }
            }
        }
    }
}

static float draw_cp_ttf(Raster *r, int cp, float pen, int slot, int fake_italic, float ph,
                         float xratio, int x0, int y0, double ca, double sa, int mirror,
                         uint32_t rgb, float a) {
    FcTtfGlyph g;
    float ascent;
    int i, j;
    if (slot < 0 || !fc_ttf_glyph(slot, ph, xratio, cp, &g)) return 0;
    ascent = fc_ttf_ascent(slot, ph);
    for (j = 0; j < g.h; j++) {
        for (i = 0; i < g.w; i++) {
            float cov, gx, gy;
            int qx, qy;
            if (!g.cover) break;
            cov = g.cover[j * g.w + i] / 255.0f;
            if (cov < 0.03f) continue;
            gx = pen + (float)g.xoff + (float)i;
            gy = ascent + (float)g.yoff + (float)j;
            if (fake_italic) gx += 0.20f * (ascent - gy);
            if (mirror) gx = -gx;
            qx = x0 + (int)floor(gx * ca - gy * sa + 0.5);
            qy = y0 + (int)floor(gx * sa + gy * ca + 0.5);
            put_px(r, qx, qy, rgb, a * cov);
        }
    }
    return g.advance > 0 ? (float)g.advance : ph * 0.3f;
}

static void draw_plain_run(Raster *r, const char *s, int n, float *pen, float sx, float sy, int mono,
                           int italic, int bold, int x0, int y0, double ca, double sa, int mirror,
                           uint32_t rgb, float a, int slot, int fake_italic, float ph,
                           float xratio) {
    const char *p = s;
    const char *end = s + n;
    while (p < end) {
        int cp = fc_utf8_next(&p, end);
        float adv = 0;
        if (!cp) break;
        if (slot >= 0) adv = draw_cp_ttf(r, cp, *pen, slot, fake_italic, ph, xratio, x0, y0, ca, sa,
                                         mirror, rgb, a);
        if (adv <= 0)
            adv = draw_cp_stroke(r, cp, *pen, sx, sy, mono, italic, bold, x0, y0, ca, sa, mirror,
                                 rgb, a);
        *pen += adv;
    }
}

static void draw_math_run(Raster *r, const char *latex, int n, int display, float *pen, float em,
                          int x0, int y0, double ca, double sa, int mirror, uint32_t rgb, float a) {
    FcTexGeom g;
    char *buf;
    int i;
    float base, sx, sy;
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) return;
    memcpy(buf, latex, (size_t)n);
    buf[n] = 0;
    sx = em / 12.0f;
    sy = em / 12.0f;
    if (!fc_tex_compile(buf, display, &g) || !g.ok) {
        *pen += draw_cp_stroke(r, '$', *pen, sx, sy, 0, 0, 0, x0, y0, ca, sa, mirror, rgb, a);
        draw_plain_run(r, buf, n, pen, sx, sy, 0, 0, 0, x0, y0, ca, sa, mirror, rgb, a, -1, 0, 0,
                       1);
        *pen += draw_cp_stroke(r, '$', *pen, sx, sy, 0, 0, 0, x0, y0, ca, sa, mirror, rgb, a);
        free(buf);
        fc_tex_geom_free(&g);
        return;
    }
    base = em * 0.78f;
    for (i = 0; i < g.n; i++) {
        FcTexOp *o = &g.ops[i];
        float gx = *pen + o->x * em;
        if (o->kind == 0) {
            float gh = o->size * em;
            float gy = base - o->y * em - 0.78f * gh;
            if (o->glyph >= 32 && o->glyph <= 126) {
                float s = gh / 12.0f;
                if (s < 0.35f) s = 0.35f;
                draw_cp_stroke(r, o->glyph, gx, s, s, 0, o->italic, o->bold, x0, y0 + (int)gy, ca,
                               sa, mirror, rgb, a);
            } else {
                float gw = (o->w > 0 ? o->w : 0.62f * o->size) * em;
                if (gw < 3) gw = 3;
                if (gh < 4) gh = 4;
                blit_bitmap_glyph(r, x0, y0, ca, sa, mirror, o->glyph, gx, gy, gw, gh, o->italic,
                                  o->bold, rgb, a);
            }
        } else {
            float rh = o->h * em;
            float rw = o->w * em;
            float gy = base - o->y * em - rh * 0.5f;
            blit_rule(r, x0, y0, ca, sa, mirror, gx, gy, rw, rh, rgb, a);
        }
    }
    *pen += g.w * em;
    fc_tex_geom_free(&g);
    free(buf);
}

static void draw_text(Raster *r, const FcLabel *l, uint32_t rgb, float a) {
    const char *s = l->text ? l->text : "";
    int x0, y0, mirror, bold, italic, mono, slot, fake_it = 0;
    double ang, ca, sa;
    float pen = 0, sx, sy, em, ph, xratio;
    if (!*s) return;
    sy = (float)(l->size_y * r->scale / 12.0);
    sx = (float)(l->size_x * r->scale / 10.0);
    if (sy < 0.35f) sy = 0.35f;
    if (sx < 0.30f) sx = 0.30f;
    em = (float)(l->size_y * r->scale);
    if (em < 6) em = 6;
    ph = (float)(l->size_y * r->scale * 1.15);
    if (ph < 8) ph = 8;
    xratio = 1.0f;
    if (l->size_y > 0.1)
        xratio = (float)((l->size_x / l->size_y) * (4.0 / 3.0)); /* 1.0 at default 3×4 */
    if (xratio < 0.4f) xratio = 0.4f;
    if (xratio > 2.5f) xratio = 2.5f;
    x0 = mapx(r, l->p.x);
    y0 = mapy(r, l->p.y);
    ang = l->rotation * 3.141592653589793 / 180.0;
    ca = cos(ang);
    sa = sin(ang);
    mirror = (l->style & FC_STYLE_MIRROR) != 0;
    bold = (l->style & FC_STYLE_BOLD) != 0;
    italic = (l->style & FC_STYLE_ITALIC) != 0;
    mono = fc_font_is_mono(l->font);
    slot = -1;
    if (fc_ttf_ready())
        slot = fc_ttf_resolve(fc_font_family(l->font), bold, italic, &fake_it);
    if (!r->tex || !fc_tex_has_math(s)) {
        draw_plain_run(r, s, (int)fc_strlen(s), &pen, sx, sy, mono, italic, bold, x0, y0, ca, sa,
                       mirror, rgb, a, slot, fake_it, ph, xratio);
        return;
    }
    {
        FcTexSeg segs[32];
        int ns = fc_tex_split(s, segs, 32);
        int i;
        for (i = 0; i < ns; i++) {
            if (segs[i].math)
                draw_math_run(r, segs[i].p, segs[i].n, segs[i].math == 2, &pen, em, x0, y0, ca, sa,
                              mirror, rgb, a);
            else
                draw_plain_run(r, segs[i].p, segs[i].n, &pen, sx, sy, mono, italic, bold, x0, y0,
                               ca, sa, mirror, rgb, a, slot, fake_it, ph, xratio);
        }
    }
}

static void draw_prim(Raster *r, const FcDrawing *d, const FcPrim *p) {
    uint32_t rgb;
    float a;
    int lw, lwc, dash;
    if (p->layer < 0 || p->layer >= FC_MAX_LAYERS) return;
    rgb = d->layers[p->layer].rgb;
    a = d->layers[p->layer].alpha;
    lw = (int)floor(d->cfg.line_width * r->scale + 0.5);
    if (lw < 1) lw = 1;
    lwc = (int)floor(d->cfg.line_width_circles * r->scale + 0.5);
    if (lwc < 1) lwc = 1;
    dash = p->ext.dash;
    switch (p->kind) {
    case FC_LINE:
        line_aa(r, mapx(r, p->u.line.a.x), mapy(r, p->u.line.a.y), mapx(r, p->u.line.b.x),
                mapy(r, p->u.line.b.y), rgb, a, lw, dash);
        if (p->ext.arrows & FC_ARROW_START)
            arrow_head(r, p->u.line.b, p->u.line.a, p->ext.arrow_len, p->ext.arrow_half_w, rgb, a,
                       p->ext.arrow_style & FC_ARROW_EMPTY);
        if (p->ext.arrows & FC_ARROW_END)
            arrow_head(r, p->u.line.a, p->u.line.b, p->ext.arrow_len, p->ext.arrow_half_w, rgb, a,
                       p->ext.arrow_style & FC_ARROW_EMPTY);
        break;
    case FC_BEZIER:
        bezier(r, p->u.bezier.p[0], p->u.bezier.p[1], p->u.bezier.p[2], p->u.bezier.p[3], rgb, a, lw,
               dash);
        break;
    case FC_RECT:
        if (p->filled)
            fill_rect(r, mapx(r, p->u.box.a.x), mapy(r, p->u.box.a.y), mapx(r, p->u.box.b.x),
                      mapy(r, p->u.box.b.y), rgb, a);
        else
            stroke_rect(r, mapx(r, p->u.box.a.x), mapy(r, p->u.box.a.y), mapx(r, p->u.box.b.x),
                        mapy(r, p->u.box.b.y), rgb, a, lw, dash);
        break;
    case FC_OVAL:
        ellipse(r, mapx(r, p->u.box.a.x), mapy(r, p->u.box.a.y), mapx(r, p->u.box.b.x),
                mapy(r, p->u.box.b.y), rgb, a, lwc, dash, p->filled);
        break;
    case FC_POLY:
        if (p->filled) fill_poly(r, p->u.poly.pts, p->u.poly.n, rgb, a);
        stroke_poly(r, p->u.poly.pts, p->u.poly.n, 1, rgb, a, lw, dash);
        break;
    case FC_CURVE:
        if (p->filled) fill_poly(r, p->u.curve.pts, p->u.curve.n, rgb, a);
        spline(r, p->u.curve.pts, p->u.curve.n, p->u.curve.closed, rgb, a, lw, dash);
        break;
    case FC_CONN: {
        int rad = (int)floor((d->cfg.diameter_conn * r->scale) / 2.0 + 0.5);
        if (rad < 1) rad = 1;
        fill_circle(r, mapx(r, p->u.conn.p.x), mapy(r, p->u.conn.p.y), rad, rgb, a);
        break;
    }
    case FC_PCB_LINE: {
        int tw = (int)floor(p->u.pcb_line.width * r->scale + 0.5);
        if (tw < 1) tw = 1;
        line_aa(r, mapx(r, p->u.pcb_line.a.x), mapy(r, p->u.pcb_line.a.y), mapx(r, p->u.pcb_line.b.x),
                mapy(r, p->u.pcb_line.b.y), rgb, a, tw, 0);
        break;
    }
    case FC_PCB_PAD: {
        int rx = (int)floor(p->u.pad.rx * r->scale / 2.0 + 0.5);
        int ry = (int)floor(p->u.pad.ry * r->scale / 2.0 + 0.5);
        int cx = mapx(r, p->u.pad.p.x), cy = mapy(r, p->u.pad.p.y);
        if (p->u.pad.style == FC_PAD_RECT)
            fill_rect(r, cx - rx, cy - ry, cx + rx, cy + ry, rgb, a);
        else
            ellipse(r, cx - rx, cy - ry, cx + rx, cy + ry, rgb, a, 1, 0, 1);
        if (p->u.pad.hole > 0) {
            int hr = (int)floor(p->u.pad.hole * r->scale / 2.0 + 0.5);
            fill_circle(r, cx, cy, hr, 0xFFFFFF, 1);
        }
        break;
    }
    case FC_TEXT:
        draw_text(r, &p->u.text, rgb, a);
        break;
    case FC_MACRO:
        break;
    }
    if (p->has_name) draw_text(r, &p->name, rgb, a);
    if (p->has_value) draw_text(r, &p->value, rgb, a);
}

static void png_write_cb(void *ctx, void *data, int size) {
    fwrite(data, 1, (size_t)size, (FILE *)ctx);
}

static int write_png(FILE *fp, const Raster *r) {
    int ok = stbi_write_png_to_func(png_write_cb, fp, r->w, r->h, 3, r->px, r->w * 3);
    return ok ? 0 : -1;
}

static int write_ppm(FILE *fp, const Raster *r) {
    if (fprintf(fp, "P6\n%d %d\n255\n", r->w, r->h) < 0) return -1;
    if (fwrite(r->px, 1, (size_t)r->w * (size_t)r->h * 3, fp) !=
        (size_t)r->w * (size_t)r->h * 3)
        return -1;
    return 0;
}

void fc_render_opts_init(FcRenderOpts *o) {
    memset(o, 0, sizeof *o);
    o->margin = 12;
    o->max_w = 1200;
    o->max_h = 900;
    o->bg = 0xFFFFFF;
    o->tex = 1;
}

int fc_render_file(const FcDrawing *d, const FcLib *lib, const FcRenderOpts *opts,
                   const char *path) {
    FcRenderOpts o;
    FcDrawing exp;
    const FcDrawing *src = d;
    FcRect bb;
    Raster r;
    double bw, bh;
    int i, x, y;
    FILE *fp;
    int rc = FC_OK;
    int own = 0;

    fc_render_opts_init(&o);
    fc_ttf_init();
    if (opts) {
        if (opts->scale) o.scale = opts->scale;
        if (opts->margin) o.margin = opts->margin;
        if (opts->max_w) o.max_w = opts->max_w;
        if (opts->max_h) o.max_h = opts->max_h;
        o.grid = opts->grid;
        if (opts->bg) o.bg = opts->bg;
        o.ppm = opts->ppm;
        o.tex = opts->tex;
    }
    fc_drawing_init(&exp);
    if (lib) {
        fc_expand(d, &exp, lib);
        src = &exp;
        own = 1;
    }
    if (!fc_bounds(src, NULL, &bb)) {
        bb.min_x = bb.min_y = 0;
        bb.max_x = bb.max_y = 20;
    }
    bw = bb.max_x - bb.min_x;
    bh = bb.max_y - bb.min_y;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    if (o.scale <= 0) {
        int mw = o.max_w > 2 * o.margin ? o.max_w - 2 * o.margin : 400;
        int mh = o.max_h > 2 * o.margin ? o.max_h - 2 * o.margin : 300;
        double sx = mw / bw, sy = mh / bh;
        o.scale = (int)floor((sx < sy ? sx : sy));
        if (o.scale < 2) o.scale = 2;
        if (o.scale > 16) o.scale = 16;
    }
    r.scale = (double)o.scale;
    r.w = (int)ceil(bw * r.scale) + 2 * o.margin;
    r.h = (int)ceil(bh * r.scale) + 2 * o.margin;
    if (r.w < 16) r.w = 16;
    if (r.h < 16) r.h = 16;
    if (r.w > 8192) r.w = 8192;
    if (r.h > 8192) r.h = 8192;
    r.ox = o.margin - bb.min_x * r.scale;
    r.oy = o.margin - bb.min_y * r.scale;
    r.dash_phase = 0;
    r.tex = o.tex;
    r.px = (uint8_t *)malloc((size_t)r.w * (size_t)r.h * 3);
    if (!r.px) {
        if (own) fc_drawing_free(&exp);
        return FC_ERR_NOMEM;
    }
    {
        uint8_t R = (uint8_t)((o.bg >> 16) & 255), G = (uint8_t)((o.bg >> 8) & 255),
                  B = (uint8_t)(o.bg & 255);
        for (y = 0; y < r.h; y++)
            for (x = 0; x < r.w; x++) {
                uint8_t *p = r.px + ((size_t)y * r.w + x) * 3;
                p[0] = R;
                p[1] = G;
                p[2] = B;
            }
    }
    if (o.grid) {
        int gx, gy;
        int step = o.scale >= 4 ? 5 : 10;
        for (gx = (int)floor(bb.min_x / step) * step; gx <= (int)ceil(bb.max_x); gx += step)
            line_aa(&r, mapx(&r, gx), mapy(&r, bb.min_y), mapx(&r, gx), mapy(&r, bb.max_y), 0xEEEEEE,
                    1, 1, 0);
        for (gy = (int)floor(bb.min_y / step) * step; gy <= (int)ceil(bb.max_y); gy += step)
            line_aa(&r, mapx(&r, bb.min_x), mapy(&r, gy), mapx(&r, bb.max_x), mapy(&r, gy), 0xEEEEEE,
                    1, 1, 0);
    }
    for (i = 0; i < src->nprims; i++) draw_prim(&r, src, &src->prims[i]);

    if (!path || fc_streq(path, "-"))
        fp = stdout;
    else
        fp = fopen(path, "wb");
    if (!fp) {
        free(r.px);
        if (own) fc_drawing_free(&exp);
        return FC_ERR_IO;
    }
    if (o.ppm) {
        if (write_ppm(fp, &r) != 0) rc = FC_ERR_IO;
    } else {
        if (write_png(fp, &r) != 0) rc = FC_ERR_IO;
    }
    if (fp != stdout) fclose(fp);
    free(r.px);
    if (own) fc_drawing_free(&exp);
    return rc;
}
