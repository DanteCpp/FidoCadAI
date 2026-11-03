#include "scene.h"
#include <QPainter>
#include <QHash>
#include <algorithm>
#include <cmath>

namespace {
QPointF point(FcPoint p) { return {p.x, p.y}; }
QPainterPath segment(QPointF a, QPointF b) {
    QPainterPath p(a);
    p.lineTo(b);
    return p;
}
QPen stroke(QColor color, double width, int dash) {
    QPen pen(color, std::max(width, 0.001), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QList<qreal> pattern;
    switch (dash) {
    case 1: pattern = {5, 5}; break;
    case 2: pattern = {2, 2}; break;
    case 3: pattern = {2, 5}; break;
    case 4: pattern = {2, 5, 5, 5}; break;
    }
    if (!pattern.empty()) {
        for (qreal &v : pattern) v /= pen.widthF();
        pen.setDashPattern(pattern);
        pen.setCapStyle(Qt::FlatCap);
    }
    return pen;
}
}

void Scene::path(const QPainterPath &shape, const QPen &pen, const QBrush &brush) {
    Item item;
    item.path = shape;
    item.pen = pen;
    item.brush = brush;
    double pad = pen.style() == Qt::NoPen ? 0 : pen.widthF() / 2;
    item.bounds = shape.boundingRect().adjusted(-pad, -pad, pad, pad);
    items_.push_back(std::move(item));
}

void Scene::label(const FcLabel &label, QColor color, bool tex, double textureScale) {
    if (!label.text || !*label.text) return;
    /* Retain the core's font metrics, stroke fonts, rotation, mirroring and TeX
     * layout. Rasterize only this label's coverage once, then upload it as a
     * transparent texture. The GPU blends it over any underlying geometry. */
    FcRect b;
    fc_label_bounds(&label, tex, &b);
    QRectF bounds(QPointF(b.min_x, b.min_y), QPointF(b.max_x, b.max_y));
    bounds = bounds.adjusted(-1, -1, 1, 1);
    if (bounds.width() <= 0 || bounds.height() <= 0) return;
    double resolution = std::min({std::max(textureScale, 1.0),
                                  2048.0 / bounds.width(), 2048.0 / bounds.height()});
    int w = std::max(1, int(std::ceil(bounds.width() * resolution)));
    int h = std::max(1, int(std::ceil(bounds.height() * resolution)));
    QImage mask(w, h, QImage::Format_RGB888);
    FcDrawing single;
    fc_drawing_init(&single);
    FcPrim primitive{};
    primitive.kind = FC_TEXT;
    primitive.u.text = label;
    single.prims = &primitive;
    single.nprims = 1;
    single.layers[0].rgb = 0;
    single.layers[0].alpha = 1;
    FcView view{};
    view.ox = -bounds.left() * resolution;
    view.oy = -bounds.top() * resolution;
    view.scale = resolution;
    view.bg = 0xffffff;
    view.tex = tex;
    fc_render_rgb(&single, nullptr, &view, mask.bits(), w, h, int(mask.bytesPerLine()));
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        const uchar *src = mask.constScanLine(y);
        auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int a = (255 - src[x * 3]) * color.alpha() / 255;
            dst[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), a));
        }
    }
    Item item;
    item.image = std::move(image);
    item.target = QRectF(bounds.topLeft(), QSizeF(w / resolution, h / resolution));
    item.bounds = item.target;
    items_.push_back(std::move(item));
}

void Scene::rebuild(const FcDrawing &drawing, const FcLib *lib,
                    const std::array<QColor, FC_MAX_LAYERS> &colors, bool tex,
                    double textureScale) {
    items_.clear();
    FcDrawing flat;
    fc_drawing_init(&flat);
    fc_expand(&drawing, &flat, lib);
    QHash<const FcImage *, QImage> images;
    for (int i = 0; i < flat.nprims; ++i) {
        const FcPrim &q = flat.prims[i];
        if (q.layer < 0 || q.layer >= FC_MAX_LAYERS) continue;
        QColor color = colors[q.layer];
        color.setAlphaF(std::clamp(double(flat.layers[q.layer].alpha), 0.0, 1.0));
        double width = q.ext.width > 0 ? q.ext.width : flat.cfg.line_width;
        QPen pen = stroke(color, width, q.ext.dash);
        QPainterPath shape;
        QPolygonF arrowPoints;
        bool closed = false;
        switch (q.kind) {
        case FC_LINE:
            shape = segment(point(q.u.line.a), point(q.u.line.b));
            arrowPoints << point(q.u.line.a) << point(q.u.line.b);
            path(shape, pen);
            break;
        case FC_BEZIER:
            shape.moveTo(point(q.u.bezier.p[0]));
            shape.cubicTo(point(q.u.bezier.p[1]), point(q.u.bezier.p[2]), point(q.u.bezier.p[3]));
            for (auto p : q.u.bezier.p) arrowPoints << point(p);
            path(shape, pen);
            break;
        case FC_RECT:
        case FC_OVAL: {
            QRectF rect = QRectF(point(q.u.box.a), point(q.u.box.b)).normalized();
            if (q.kind == FC_RECT) shape.addRect(rect);
            else {
                shape.addEllipse(rect);
                pen = stroke(color, q.ext.width > 0 ? q.ext.width : flat.cfg.line_width_circles,
                             q.ext.dash);
            }
            path(shape, q.filled ? QPen(Qt::NoPen) : pen,
                 q.filled ? QBrush(color) : QBrush(Qt::NoBrush));
            break;
        }
        case FC_POLY:
        case FC_CURVE: {
            int n = q.u.poly.n;
            if (!n) break;
            QPolygonF points;
            for (int k = 0; k < n; ++k) points << point(q.u.poly.pts[k]);
            closed = q.kind == FC_POLY || q.u.poly.closed;
            arrowPoints = points;
            shape.moveTo(points[0]);
            if (q.kind == FC_POLY || n < 3) {
                for (int k = 1; k < n; ++k) shape.lineTo(points[k]);
            } else {
                for (int k = 0; k < (closed ? n : n - 1); ++k) {
                    QPointF p0 = points[closed ? (k + n - 1) % n : std::max(0, k - 1)];
                    QPointF p1 = points[k], p2 = points[(k + 1) % n];
                    QPointF p3 = points[closed ? (k + 2) % n : std::min(n - 1, k + 2)];
                    shape.cubicTo(p1 + (p2 - p0) / 6, p2 - (p3 - p1) / 6, p2);
                }
            }
            if (closed) shape.closeSubpath();
            // The core fills CV/CP's vertex polygon, then strokes the spline.
            if (q.filled) {
                QPainterPath fill;
                fill.addPolygon(points);
                path(fill, Qt::NoPen, color);
            }
            path(shape, pen);
            break;
        }
        case FC_CONN:
            shape.addEllipse(point(q.u.conn.p), flat.cfg.diameter_conn / 2, flat.cfg.diameter_conn / 2);
            path(shape, Qt::NoPen, color);
            break;
        case FC_PCB_LINE:
            path(segment(point(q.u.pcb_line.a), point(q.u.pcb_line.b)),
                 stroke(color, q.u.pcb_line.width, 0));
            break;
        case FC_PCB_PAD: {
            QPointF center = point(q.u.pad.p);
            QRectF rect(center.x() - q.u.pad.rx / 2, center.y() - q.u.pad.ry / 2,
                        q.u.pad.rx, q.u.pad.ry);
            if (q.u.pad.style == FC_PAD_RECT) shape.addRect(rect);
            else shape.addEllipse(rect);
            path(shape, Qt::NoPen, color);
            if (q.u.pad.hole > 0) {
                QPainterPath hole;
                hole.addEllipse(center, q.u.pad.hole / 2, q.u.pad.hole / 2);
                path(hole, Qt::NoPen, Qt::white);
            }
            break;
        }
        case FC_TEXT:
            label(q.u.text, color, tex, textureScale);
            break;
        case FC_MACRO: {
            QPointF center = point(q.u.macro.p);
            QRectF rect(center - QPointF(4, 4), QSizeF(8, 8));
            shape.addRect(rect);
            path(shape, stroke(color, 0.25, 1));
            path(segment(rect.topLeft(), rect.bottomRight()), stroke(color, 0.25, 0));
            path(segment(rect.bottomLeft(), rect.topRight()), stroke(color, 0.25, 0));
            FcLabel text{};
            text.p = {center.x() + 4, center.y() - 4};
            text.size_x = 3;
            text.size_y = 4;
            text.font = "*";
            text.text = q.u.macro.key;
            label(text, color, tex, textureScale);
            break;
        }
        case FC_IMAGE: {
            const FcImage *bitmap = q.u.image.bitmap;
            if (!bitmap) break;
            if (!images.contains(bitmap)) {
                int w, h;
                fc_image_size(bitmap, &w, &h);
                QImage image(fc_image_pixels(bitmap), w, h, w * 4, QImage::Format_RGBA8888);
                images.insert(bitmap, image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
            }
            Item item;
            item.image = images.value(bitmap);
            item.target = QRectF(0, 0, q.u.image.width, q.u.image.height);
            item.transform.translate(q.u.image.p.x, q.u.image.p.y);
            item.transform.rotate(q.u.image.rotation);
            item.transform.scale(q.u.image.mirror ? -1 : 1, 1);
            item.bounds = item.transform.mapRect(item.target);
            item.opacity = color.alphaF() * q.u.image.opacity;
            items_.push_back(std::move(item));
            break;
        }
        }
        if (!closed && arrowPoints.size() > 1) {
            auto arrow = [&](bool start) {
                QPointF tip = start ? arrowPoints.first() : arrowPoints.last(), from = tip;
                for (int k = 1; k < arrowPoints.size() && from == tip; ++k)
                    from = arrowPoints[start ? k : arrowPoints.size() - 1 - k];
                QPointF delta = tip - from;
                double length = std::hypot(delta.x(), delta.y());
                if (length < 1e-6) return;
                QPointF direction = delta / length, normal(-direction.y(), direction.x());
                QPointF left = tip - direction * q.ext.arrow_len + normal * q.ext.arrow_half_w;
                QPointF right = tip - direction * q.ext.arrow_len - normal * q.ext.arrow_half_w;
                QPainterPath head(left);
                head.lineTo(tip);
                head.lineTo(right);
                bool empty = q.ext.arrow_style & FC_ARROW_EMPTY;
                if (!empty) head.closeSubpath();
                path(head, empty ? stroke(color, 0.25, 0) : QPen(Qt::NoPen),
                     empty ? QBrush(Qt::NoBrush) : QBrush(color));
                if (q.ext.arrow_style & FC_ARROW_LIMITER)
                    path(segment(tip + normal * q.ext.arrow_half_w, tip - normal * q.ext.arrow_half_w),
                         stroke(color, 0.25, 0));
            };
            if (q.ext.arrows & FC_ARROW_START) arrow(true);
            if (q.ext.arrows & FC_ARROW_END) arrow(false);
        }
        if (q.has_name) label(q.name, color, tex, textureScale);
        if (q.has_value) label(q.value, color, tex, textureScale);
    }
    fc_drawing_free(&flat);
}

void Scene::paint(QPainter &p, const QRectF &visible) const {
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    for (const Item &item : items_) {
        if (!visible.intersects(item.bounds)) continue;
        if (item.image.isNull()) {
            p.setPen(item.pen);
            p.setBrush(item.brush);
            p.drawPath(item.path);
        } else {
            p.save();
            p.setTransform(item.transform, true);
            p.setOpacity(item.opacity);
            p.drawImage(item.target, item.image);
            p.restore();
        }
    }
}
