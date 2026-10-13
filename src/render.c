#include "internal.h"
#include "font8x8.h"
#include "stb_image_write.h"

#include <math.h>

typedef struct {
    uint8_t *px; /* RGB */
    int w, h;
    size_t stride; /* bytes per row */
    double ox, oy; /* origin in pixels of logical (0,0) */
    double scale;
    double dash_phase; /* pixel arc length within the current primitive */
    int tex;
} Raster;

#define MAX_RASTER 8192
#define MAX_TEXT_PX 2048.0 /* glyph height cap; TTF bitmaps are capped at 512 anyway */
#define MIN_TEXT_PX 1.0    /* below this a label is skipped rather than smeared */

/* Pixel values far outside the raster are pinned here so int maths cannot overflow;
 * every drawing loop below is clipped to the raster. */
static int clampi(double v) {
    if (!(v > -1e8)) return -100000000;
    if (v > 1e8) return 100000000;
    return (int)floor(v + 0.5);
}

static void put_px(Raster *r, int x, int y, uint32_t rgb, float alpha) {
    uint8_t *p;
    int R, G, B;
    if ((unsigned)x >= (unsigned)r->w || (unsigned)y >= (unsigned)r->h) return;
    if (alpha <= 0) return;
    if (alpha > 1) alpha = 1;
    p = r->px + (size_t)y * r->stride + (size_t)x * 3;
    R = (int)((rgb >> 16) & 255);
    G = (int)((rgb >> 8) & 255);
    B = (int)(rgb & 255);
    p[0] = (uint8_t)(p[0] * (1 - alpha) + R * alpha);
    p[1] = (uint8_t)(p[1] * (1 - alpha) + G * alpha);
    p[2] = (uint8_t)(p[2] * (1 - alpha) + B * alpha);
}

static int mapx(const Raster *r, double x) { return clampi(x * r->scale + r->ox); }
static int mapy(const Raster *r, double y) { return clampi(y * r->scale + r->oy); }
static int scale_len(const Raster *r, double v) { return clampi(v * r->scale); }

static int dash_on(int style, double dist) {
    /* style 0 solid; 1 dashed; 2 fine dotted; 3 dotted; 4 dash-dot */
    double p;
    if (style <= 0) return 1;
    if (dist < 0) dist = -dist;
    switch (style) {
    case 1:
        p = fmod(dist, 10);
        return p < 5;
    case 2:
        p = fmod(dist, 4);
        return p < 2;
    case 3:
        p = fmod(dist, 7);
        return p < 2;
    case 4:
        p = fmod(dist, 17);
        return p < 2 || (p >= 7 && p < 12);
    default:
        return 1;
    }
}

static void plot(Raster *r, int x, int y, uint32_t rgb, float a, int dash, double dist) {
    if (dash_on(dash, dist / r->scale)) put_px(r, x, y, rgb, a);
}

static void fill_disk(Raster *r, int cx, int cy, int rad, uint32_t rgb, float a) {
    int x, y;
    if (rad < 1) return;
    for (y = -rad; y <= rad; y++)
        for (x = -rad; x <= rad; x++)
            if (x * x + y * y <= rad * rad) put_px(r, cx + x, cy + y, rgb, a);
}

/* Liang-Barsky: clips the segment to [xmin,xmax]x[ymin,ymax]; t0 gets the start
 * parameter of the kept part. Returns 0 when nothing is left. */
static int clip_seg(double *x0, double *y0, double *x1, double *y1, double xmin, double ymin,
                    double xmax, double ymax, double *t0_out) {
    double dx = *x1 - *x0, dy = *y1 - *y0;
    double p[4], q[4], t0 = 0, t1 = 1;
    int i;
    p[0] = -dx; q[0] = *x0 - xmin;
    p[1] = dx;  q[1] = xmax - *x0;
    p[2] = -dy; q[2] = *y0 - ymin;
    p[3] = dy;  q[3] = ymax - *y0;
    for (i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return 0;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0) {
                if (t > t1) return 0;
                if (t > t0) t0 = t;
            } else {
                if (t < t0) return 0;
                if (t < t1) t1 = t;
            }
        }
    }
    *x1 = *x0 + t1 * dx;
    *y1 = *y0 + t1 * dy;
    *x0 += t0 * dx;
    *y0 += t0 * dy;
    *t0_out = t0;
    return 1;
}

static void line_aa(Raster *r, int x0, int y0, int x1, int y1, uint32_t rgb, float a, int w,
                    int dash) {
    int dx, sx, dy, sy, err, e2;
    double dist = r->dash_phase, step = 0;
    int ax, ay, bx, by;
    int thick = w < 1 ? 1 : w;
    int lim = 2 * (r->w + r->h) + 2;
    if (thick > lim) thick = lim;
    {
        /* only the part within half a pen width of the raster can leave pixels */
        double pad = thick / 2 + 1, t0 = 0;
        double fx0 = x0, fy0 = y0, fx1 = x1, fy1 = y1;
        double length = hypot(fx1 - fx0, fy1 - fy0);
        if (dash) r->dash_phase += length;
        if (!clip_seg(&fx0, &fy0, &fx1, &fy1, -pad, -pad, r->w - 1 + pad, r->h - 1 + pad, &t0))
            return;
        dist += t0 * length; /* preserve phase when clipped */
        x0 = (int)floor(fx0 + 0.5);
        y0 = (int)floor(fy0 + 0.5);
        x1 = (int)floor(fx1 + 0.5);
        y1 = (int)floor(fy1 + 0.5);
    }
    ax = x0;
    ay = y0;
    bx = x1;
    by = y1;
    dx = abs(x1 - x0);
    sx = x0 < x1 ? 1 : -1;
    dy = -abs(y1 - y0);
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    if (dx || dy) step = hypot(dx, dy) / (dx > -dy ? dx : -dy);
    for (;;) {
        int t, tlo = -(thick / 2), thi = thick / 2;
        /* pen offsets that stay on the raster */
        if (dx > -dy) {
            if (tlo < -y0) tlo = -y0;
            if (thi > r->h - 1 - y0) thi = r->h - 1 - y0;
        } else {
            if (tlo < -x0) tlo = -x0;
            if (thi > r->w - 1 - x0) thi = r->w - 1 - x0;
        }
        for (t = tlo; t <= thi; t++) {
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
        dist += step;
    }
    /* FidoCadJS strokes with lineCap/lineJoin = round (GraphicsCanvas): a
     * solid, thick line gets a disk at both ends, which also rounds the
     * corners of polylines, polygons and rectangles drawn edge by edge. */
    if (!dash && thick > 2) {
        int rad = thick / 2;
        fill_disk(r, ax, ay, rad, rgb, a);
        fill_disk(r, bx, by, rad, rgb, a);
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
    minx = clampi(floor(cx - rx) - w);
    maxx = clampi(ceil(cx + rx) + w);
    miny = clampi(floor(cy - ry) - w);
    maxy = clampi(ceil(cy + ry) + w);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > r->w - 1) maxx = r->w - 1;
    if (maxy > r->h - 1) maxy = r->h - 1;
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
                    double angle = atan2(dy, dx);
                    if (angle < 0) angle += 6.283185307179586;
                    double dist = angle * (rx + ry) / 2;
                    plot(r, xi, yi, rgb, a, dash, dist);
                }
            }
        }
    }
}

static void fill_circle(Raster *r, int cx, int cy, int rad, uint32_t rgb, float a) {
    long long x, y, r2 = (long long)rad * rad;
    long long x0 = -(long long)rad, x1 = rad, y0 = -(long long)rad, y1 = rad;
    if (x0 < -(long long)cx) x0 = -(long long)cx;
    if (x1 > (long long)r->w - 1 - cx) x1 = (long long)r->w - 1 - cx;
    if (y0 < -(long long)cy) y0 = -(long long)cy;
    if (y1 > (long long)r->h - 1 - cy) y1 = (long long)r->h - 1 - cy;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (x * x + y * y <= r2) put_px(r, (int)(cx + x), (int)(cy + y), rgb, a);
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
                int xx = x0 + (int)((long long)(x1 - x0) * (y - y0) / (den ? den : 1));
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
                       float a, int style) {
    int empty = (style & FC_ARROW_EMPTY) != 0;
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
    if (style & FC_ARROW_LIMITER) {
        /* bar across the tip */
        line_aa(r, mapx(r, to.x + px * half), mapy(r, to.y + py * half), mapx(r, to.x - px * half),
                mapy(r, to.y - py * half), rgb, a, 1, 0);
    }
}

/* Picks the first point of pts[] (walking away from the tip) that differs from it,
 * so a control point sitting on the endpoint still gives a direction. */
static int arrow_from(const FcPoint *pts, int n, FcPoint *out) {
    int i;
    for (i = 1; i < n; i++) {
        if (pts[i].x != pts[0].x || pts[i].y != pts[0].y) {
            *out = pts[i];
            return 1;
        }
    }
    return 0;
}

static void draw_arrows(Raster *r, const FcPrim *p, const FcPoint *head, const FcPoint *tail, int n,
                        uint32_t rgb, float a) {
    FcPoint from;
    if ((p->ext.arrows & FC_ARROW_START) && arrow_from(head, n, &from))
        arrow_head(r, from, head[0], p->ext.arrow_len, p->ext.arrow_half_w, rgb, a,
                   p->ext.arrow_style);
    if ((p->ext.arrows & FC_ARROW_END) && arrow_from(tail, n, &from))
        arrow_head(r, from, tail[0], p->ext.arrow_len, p->ext.arrow_half_w, rgb, a,
                   p->ext.arrow_style);
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

static float draw_cp_ttf(Raster *r, int cp, float pen, float base, int slot, int fake_italic,
                         float ph, float xratio, int x0, int y0, double ca, double sa, int mirror,
                         uint32_t rgb, float a) {
    FcTtfGlyph g;
    float ascent, bx, by;
    int i, j, ix, iy;
    if (slot < 0) return 0;
    /* Split the pen and the baseline into whole pixels plus a sub-pixel shift
       that goes into the glyph bitmap, so text glides instead of snapping. */
    ascent = fc_ttf_ascent(slot, ph);
    bx = floorf(pen * FC_TTF_SUBPIX + 0.5f) / FC_TTF_SUBPIX;
    by = floorf((base + ascent) * FC_TTF_SUBPIX + 0.5f) / FC_TTF_SUBPIX;
    ix = (int)floorf(bx);
    iy = (int)floorf(by);
    if (!fc_ttf_glyph(slot, ph, xratio, cp, bx - (float)ix, by - (float)iy, &g)) return 0;
    for (j = 0; j < g.h; j++) {
        for (i = 0; i < g.w; i++) {
            float cov, gx, gy;
            int qx, qy;
            if (!g.cover) break;
            cov = g.cover[j * g.w + i] / 255.0f;
            if (cov < 0.03f) continue;
            gx = (float)(ix + g.xoff + i);
            gy = (float)(iy + g.yoff + j);
            if (fake_italic) gx += 0.20f * (by - gy);
            if (mirror) gx = -gx;
            qx = x0 + (int)floor(gx * ca - gy * sa + 0.5);
            qy = y0 + (int)floor(gx * sa + gy * ca + 0.5);
            put_px(r, qx, qy, rgb, a * cov);
        }
    }
    return g.advance > 0 ? g.advance : ph * 0.3f;
}

static void draw_plain_run(Raster *r, const char *s, int n, float *pen, float base, float sx,
                           float sy, int mono, int italic, int bold, int x0, int y0, double ca,
                           double sa, int mirror, uint32_t rgb, float a, int slot,
                           int fake_italic, float ph, float xratio) {
    const char *p = s;
    const char *end = s + n;
    while (p < end) {
        int cp = fc_utf8_next(&p, end);
        float adv = 0;
        if (!cp) break;
        if (slot >= 0)
            adv = draw_cp_ttf(r, cp, *pen, base, slot, fake_italic, ph, xratio, x0, y0, ca, sa,
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
    if (!fc_tex_compile(buf, display, &g)) {
        *pen += draw_cp_stroke(r, '$', *pen, sx, sy, 0, 0, 0, x0, y0, ca, sa, mirror, rgb, a);
        draw_plain_run(r, buf, n, pen, 0, sx, sy, 0, 0, 0, x0, y0, ca, sa, mirror, rgb, a, -1, 0,
                       0, 1);
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
                draw_cp_stroke(r, o->glyph, gx, s, s, 0, o->italic, o->bold, x0, y0 + (int)gy, ca,
                               sa, mirror, rgb, a);
            } else {
                float gw = (o->w > 0 ? o->w : 0.62f * o->size) * em;
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
    float pen = 0, base = 0, sx, sy, em, ph, xratio;
    double hpx, wpx, fx, fy;
    if (!*s) return;
    /* Text scales with the view and nothing else: no pixel floors, and the cap
       shrinks height and width together, so zooming never changes its shape or
       its size relative to the drawing. */
    hpx = l->size_y * r->scale;
    wpx = l->size_x * r->scale;
    if (hpx > MAX_TEXT_PX || wpx > MAX_TEXT_PX) {
        double k = MAX_TEXT_PX / (hpx > wpx ? hpx : wpx);
        hpx *= k;
        wpx *= k;
    }
    if (hpx < MIN_TEXT_PX) return; /* thinner than a pixel row: nothing to see */
    sy = (float)(hpx / 12.0);
    sx = (float)(wpx / 10.0);
    em = (float)hpx;
    ph = (float)(hpx * 1.15);
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
    /* the anchor's sub-pixel remainder, rotated back into text space */
    fx = l->p.x * r->scale + r->ox - x0;
    fy = l->p.y * r->scale + r->oy - y0;
    if (fabs(fx) <= 1 && fabs(fy) <= 1) {
        pen = (float)(fx * ca + fy * sa);
        base = (float)(fy * ca - fx * sa);
        if (mirror) pen = -pen;
    }
    bold = (l->style & FC_STYLE_BOLD) != 0;
    italic = (l->style & FC_STYLE_ITALIC) != 0;
    mono = fc_font_is_mono(l->font);
    slot = -1;
    if (fc_ttf_ready())
        slot = fc_ttf_resolve(fc_font_family(l->font), bold, italic, &fake_it);
    if (!r->tex || !fc_tex_has_math(s)) {
        draw_plain_run(r, s, (int)fc_strlen(s), &pen, base, sx, sy, mono, italic, bold, x0, y0,
                       ca, sa, mirror, rgb, a, slot, fake_it, ph, xratio);
        return;
    }
    {
        /* each segment consumes at least one '$', plus at most one text run each */
        const char *q;
        int maxn = 2, ns, i;
        FcTexSeg *segs;
        for (q = s; *q; q++)
            if (*q == '$') maxn += 2;
        segs = (FcTexSeg *)malloc((size_t)maxn * sizeof *segs);
        if (!segs) return;
        ns = fc_tex_split(s, segs, maxn);
        for (i = 0; i < ns; i++) {
            if (segs[i].math)
                draw_math_run(r, segs[i].p, segs[i].n, segs[i].math == 2, &pen, em, x0, y0, ca, sa,
                              mirror, rgb, a);
            else
                draw_plain_run(r, segs[i].p, segs[i].n, &pen, base, sx, sy, mono, italic, bold,
                               x0, y0, ca, sa, mirror, rgb, a, slot, fake_it, ph, xratio);
        }
        free(segs);
    }
}

/* Unresolved macro: dashed box with a cross at the insertion point, plus the key, so
 * the missing part is visible in the picture instead of silently absent. */
static void draw_placeholder(Raster *r, const FcPrim *p, uint32_t rgb, float a) {
    const double h = FC_PLACEHOLDER_SIZE;
    FcPoint c = p->u.macro.p;
    int x0 = mapx(r, c.x - h), y0 = mapy(r, c.y - h), x1 = mapx(r, c.x + h), y1 = mapy(r, c.y + h);
    stroke_rect(r, x0, y0, x1, y1, rgb, a, 1, 1);
    line_aa(r, x0, y0, x1, y1, rgb, a, 1, 0);
    line_aa(r, x0, y1, x1, y0, rgb, a, 1, 0);
    if (p->u.macro.key && p->u.macro.key[0]) {
        FcLabel l;
        fc_label_default(&l, c.x + h, c.y - h, p->layer);
        l.text = p->u.macro.key;
        draw_text(r, &l, rgb, a);
    }
}

static void draw_prim(Raster *r, const FcDrawing *d, const FcPrim *p) {
    r->dash_phase = 0;
    uint32_t rgb;
    float a;
    int lw, lwc, dash;
    if (p->layer < 0 || p->layer >= FC_MAX_LAYERS) return;
    rgb = d->layers[p->layer].rgb;
    a = d->layers[p->layer].alpha;
    lw = scale_len(r, p->ext.width > 0 ? p->ext.width : d->cfg.line_width);
    if (lw < 1) lw = 1;
    lwc = scale_len(r, p->ext.width > 0 ? p->ext.width : d->cfg.line_width_circles);
    if (lwc < 1) lwc = 1;
    dash = p->ext.dash;
    switch (p->kind) {
    case FC_LINE:
        line_aa(r, mapx(r, p->u.line.a.x), mapy(r, p->u.line.a.y), mapx(r, p->u.line.b.x),
                mapy(r, p->u.line.b.y), rgb, a, lw, dash);
        if (p->ext.arrows) {
            FcPoint head[2], tail[2];
            head[0] = tail[1] = p->u.line.a;
            head[1] = tail[0] = p->u.line.b;
            draw_arrows(r, p, head, tail, 2, rgb, a);
        }
        break;
    case FC_BEZIER:
        bezier(r, p->u.bezier.p[0], p->u.bezier.p[1], p->u.bezier.p[2], p->u.bezier.p[3], rgb, a, lw,
               dash);
        if (p->ext.arrows) {
            const FcPoint *b = p->u.bezier.p;
            FcPoint tail[4] = {b[3], b[2], b[1], b[0]};
            draw_arrows(r, p, b, tail, 4, rgb, a);
        }
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
        if (p->ext.arrows && !p->u.curve.closed && p->u.curve.n >= 2) {
            /* open Catmull-Rom leaves each end along its first/last chord */
            int n = p->u.curve.n;
            const FcPoint *c = p->u.curve.pts;
            FcPoint head[2] = {c[0], c[1]}, tail[2] = {c[n - 1], c[n - 2]};
            draw_arrows(r, p, head, tail, 2, rgb, a);
        }
        break;
    case FC_CONN: {
        int rad = scale_len(r, d->cfg.diameter_conn / 2.0);
        if (rad < 1) rad = 1;
        fill_circle(r, mapx(r, p->u.conn.p.x), mapy(r, p->u.conn.p.y), rad, rgb, a);
        break;
    }
    case FC_PCB_LINE: {
        int tw = scale_len(r, p->u.pcb_line.width);
        if (tw < 1) tw = 1;
        line_aa(r, mapx(r, p->u.pcb_line.a.x), mapy(r, p->u.pcb_line.a.y), mapx(r, p->u.pcb_line.b.x),
                mapy(r, p->u.pcb_line.b.y), rgb, a, tw, 0);
        break;
    }
    case FC_PCB_PAD: {
        int rx = abs(scale_len(r, p->u.pad.rx / 2.0));
        int ry = abs(scale_len(r, p->u.pad.ry / 2.0));
        int cx = mapx(r, p->u.pad.p.x), cy = mapy(r, p->u.pad.p.y);
        if (p->u.pad.style == FC_PAD_RECT)
            fill_rect(r, cx - rx, cy - ry, cx + rx, cy + ry, rgb, a);
        else
            ellipse(r, cx - rx, cy - ry, cx + rx, cy + ry, rgb, a, 1, 0, 1);
        if (p->u.pad.hole > 0) {
            int hr = scale_len(r, p->u.pad.hole / 2.0);
            fill_circle(r, cx, cy, hr, 0xFFFFFF, 1);
        }
        break;
    }
    case FC_TEXT:
        draw_text(r, &p->u.text, rgb, a);
        break;
    case FC_MACRO:
        draw_placeholder(r, p, rgb, a);
        break;
    }
    if (p->has_name) draw_text(r, &p->name, rgb, a);
    if (p->has_value) draw_text(r, &p->value, rgb, a);
}

static void png_write_cb(void *ctx, void *data, int size) {
    FILE *fp = (FILE *)ctx;
    if (fwrite(data, 1, (size_t)size, fp) != (size_t)size) {
        /* ferror() is checked by the caller */
    }
}

static int write_png(FILE *fp, const Raster *r) {
    int ok = stbi_write_png_to_func(png_write_cb, fp, r->w, r->h, 3, r->px, r->w * 3);
    return ok && !ferror(fp) ? 0 : -1;
}

static int write_ppm(FILE *fp, const Raster *r) {
    if (fprintf(fp, "P6\n%d %d\n255\n", r->w, r->h) < 0) return -1;
    if (fwrite(r->px, 1, (size_t)r->w * (size_t)r->h * 3, fp) !=
        (size_t)r->w * (size_t)r->h * 3)
        return -1;
    return 0;
}

static void fill_bg(Raster *r, uint32_t bg) {
    uint8_t R = (uint8_t)((bg >> 16) & 255), G = (uint8_t)((bg >> 8) & 255), B = (uint8_t)(bg & 255);
    int x, y;
    for (y = 0; y < r->h; y++) {
        uint8_t *p = r->px + (size_t)y * r->stride;
        for (x = 0; x < r->w; x++, p += 3) {
            p[0] = R;
            p[1] = G;
            p[2] = B;
        }
    }
}

int fc_render_rgb(const FcDrawing *d, const FcLib *lib, const FcView *v, uint8_t *rgb, int w,
                  int h, int stride) {
    FcDrawing exp;
    const FcDrawing *src = d;
    Raster r;
    int i;
    if (!d || !v || !rgb || w <= 0 || h <= 0 || stride < w * 3 || v->scale <= 0) return FC_ERR_ARG;
    fc_ttf_init();
    r.px = rgb;
    r.w = w;
    r.h = h;
    r.stride = (size_t)stride;
    r.ox = v->ox;
    r.oy = v->oy;
    r.scale = v->scale;
    r.tex = v->tex;
    r.dash_phase = 0;
    fill_bg(&r, v->bg);
    if (v->grid > 0 && v->grid * v->scale >= 4) {
        double step = v->grid * v->scale;
        int big = v->grid_major > 0 ? v->grid_major : 0;
        long gx0 = (long)floor(-r.ox / step), gy0 = (long)floor(-r.oy / step);
        long gx, gy;
        for (gy = gy0; gy * step + r.oy < h; gy++)
            for (gx = gx0; gx * step + r.ox < w; gx++) {
                int px = (int)floor(gx * step + r.ox + 0.5), py = (int)floor(gy * step + r.oy + 0.5);
                int major = big && gx % big == 0 && gy % big == 0;
                put_px(&r, px, py, v->grid_rgb, 1);
                if (major) {
                    put_px(&r, px + 1, py, v->grid_rgb, 1);
                    put_px(&r, px, py + 1, v->grid_rgb, 1);
                    put_px(&r, px - 1, py, v->grid_rgb, 1);
                    put_px(&r, px, py - 1, v->grid_rgb, 1);
                }
            }
    }
    fc_drawing_init(&exp);
    if (lib) {
        fc_expand(d, &exp, lib);
        src = &exp;
    }
    for (i = 0; i < src->nprims; i++) draw_prim(&r, src, &src->prims[i]);
    fc_drawing_free(&exp);
    return FC_OK;
}

void fc_render_opts_init(FcRenderOpts *o) {
    memset(o, 0, sizeof *o);
    o->margin = 12;
    o->max_w = 1200;
    o->max_h = 900;
    o->bg = 0xFFFFFF;
    o->tex = 1;
}

/* Pixels per unit when none is given: fit max_w x max_h, whole numbers when zooming
 * in (crisp lines), fractional when the drawing is too big for even 1 px/unit. */
static double auto_scale(const FcRenderOpts *o, double bw, double bh) {
    int mw = o->max_w > 2 * o->margin ? o->max_w - 2 * o->margin : 400;
    int mh = o->max_h > 2 * o->margin ? o->max_h - 2 * o->margin : 300;
    double s = mw / bw < mh / bh ? mw / bw : mh / bh;
    if (s >= 1) s = floor(s);
    if (s > 16) s = 16;
    return s;
}

int fc_render_file(const FcDrawing *d, const FcLib *lib, const FcRenderOpts *opts,
                   const char *path) {
    FcRenderOpts o;
    FcDrawing exp;
    const FcDrawing *src = d;
    FcRect bb;
    Raster r;
    double bw, bh, fit;
    int i;
    FILE *fp;
    int rc = FC_OK;
    int own = 0;

    fc_render_opts_init(&o);
    fc_ttf_init();
    if (opts) {
        o = *opts;
        if (o.max_w <= 0) o.max_w = 1200;
        if (o.max_h <= 0) o.max_h = 900;
    }
    if (o.has_crop && (!isfinite(o.crop.min_x) || !isfinite(o.crop.min_y) ||
                       !isfinite(o.crop.max_x) || !isfinite(o.crop.max_y) ||
                       o.crop.min_x >= o.crop.max_x || o.crop.min_y >= o.crop.max_y ||
                       fabs(o.crop.min_x) > FC_MAX_COORD || fabs(o.crop.min_y) > FC_MAX_COORD ||
                       fabs(o.crop.max_x) > FC_MAX_COORD || fabs(o.crop.max_y) > FC_MAX_COORD))
        return FC_ERR_ARG;
    if (o.margin < 0) o.margin = 0;
    if (o.margin > 1024) o.margin = 1024;
    o.bg &= 0xFFFFFFu;
    if (lib) {
        fc_drawing_init(&exp);
        fc_expand(d, &exp, lib);
        src = &exp;
        own = 1;
    }
    if (!fc_bounds(src, NULL, &bb)) {
        bb.min_x = bb.min_y = 0;
        bb.max_x = bb.max_y = 20;
    }
    if (o.has_crop) bb = o.crop;
    bw = bb.max_x - bb.min_x;
    bh = bb.max_y - bb.min_y;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    r.scale = o.scale > 0 ? (o.scale > 1000 ? 1000 : o.scale) : auto_scale(&o, bw, bh);
    /* shrink rather than crop when the image would exceed the raster limit */
    fit = (MAX_RASTER - 2.0 * o.margin) / (bw > bh ? bw : bh);
    if (r.scale > fit) r.scale = fit;
    r.w = (int)ceil(bw * r.scale) + 2 * o.margin;
    r.h = (int)ceil(bh * r.scale) + 2 * o.margin;
    if (r.w < 16) r.w = 16;
    if (r.h < 16) r.h = 16;
    if (r.w > MAX_RASTER) r.w = MAX_RASTER;
    if (r.h > MAX_RASTER) r.h = MAX_RASTER;
    r.ox = o.margin - bb.min_x * r.scale;
    r.oy = o.margin - bb.min_y * r.scale;
    r.tex = o.tex;
    r.dash_phase = 0;
    r.stride = (size_t)r.w * 3;
    r.px = (uint8_t *)malloc((size_t)r.w * (size_t)r.h * 3);
    if (!r.px) {
        if (own) fc_drawing_free(&exp);
        return FC_ERR_NOMEM;
    }
    fill_bg(&r, o.bg);
    if (o.grid) {
        double gx, gy;
        double step = 5;
        while (step * r.scale < 4) step *= 2; /* keep grid lines at least 4 px apart */
        for (gx = floor(bb.min_x / step) * step; gx <= ceil(bb.max_x); gx += step)
            line_aa(&r, mapx(&r, gx), mapy(&r, bb.min_y), mapx(&r, gx), mapy(&r, bb.max_y), 0xEEEEEE,
                    1, 1, 0);
        for (gy = floor(bb.min_y / step) * step; gy <= ceil(bb.max_y); gy += step)
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
    if (fp != stdout) {
        if (fclose(fp) != 0) rc = FC_ERR_IO;
    } else if (fflush(fp) != 0) {
        rc = FC_ERR_IO;
    }
    free(r.px);
    if (own) fc_drawing_free(&exp);
    return rc;
}
