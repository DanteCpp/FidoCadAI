#pragma once

#include "fidocad.h"
#include <QImage>
#include <QPainterPath>
#include <QPen>
#include <QTransform>
#include <array>
#include <vector>

/* Retained vector geometry and texture assets. View changes only change the
 * painter's transform; no viewport-sized CPU raster is involved. */
class Scene {
public:
    void rebuild(const FcDrawing &drawing, const FcLib *lib,
                 const std::array<QColor, FC_MAX_LAYERS> &colors, bool tex,
                 double textureScale);
    void paint(class QPainter &p, const QRectF &visible) const;

private:
    struct Item {
        QPainterPath path;
        QPen pen = Qt::NoPen;
        QBrush brush = Qt::NoBrush;
        QImage image;
        QRectF target, bounds;
        QTransform transform;
        double opacity = 1;
    };
    std::vector<Item> items_;
    void path(const QPainterPath &path, const QPen &pen, const QBrush &brush = Qt::NoBrush);
    void label(const FcLabel &label, QColor color, bool tex, double textureScale);
};
