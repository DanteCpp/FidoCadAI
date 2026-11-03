#include "canvas.h"
#include "keysheet.h"
#include "props.h"

#include <QApplication>
#include <QClipboard>
#include <QBuffer>
#include <QCompleter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileSystemModel>
#include <QMimeData>
#include <QUrl>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QWheelEvent>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

uint32_t toRgb(const QColor &c) { return (uint32_t)c.rgb() & 0xFFFFFFu; }

QPointF pt(const FcPoint &p) { return QPointF(p.x, p.y); }

QString toolName(Canvas::Tool t) {
    switch (t) {
    case Canvas::Select: return "select";
    case Canvas::Line: return "line";
    case Canvas::Rect: return "rectangle";
    case Canvas::Oval: return "ellipse";
    case Canvas::Dot: return "connection";
    case Canvas::Text: return "text";
    case Canvas::Macro: return "symbol";
    }
    return QString();
}

} // namespace

/* Keep event handling and inline forms on Canvas. This child supplies the
 * OpenGL paint device; it does not intercept editor input. */
class CanvasSurface : public QOpenGLWidget {
public:
    CanvasSurface(QWidget *parent, std::function<void(QPainter &)> draw)
        : QOpenGLWidget(parent), draw_(std::move(draw)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        QSurfaceFormat f = format();
        f.setSamples(4);
        f.setSwapInterval(1);
        setFormat(f);
    }
    QString renderer;
protected:
    void initializeGL() override {
        const auto *name = context()->functions()->glGetString(GL_RENDERER);
        renderer = QString::fromLatin1(reinterpret_cast<const char *>(name));
        qInfo("fcai-gui: OpenGL canvas: %s", qPrintable(renderer));
    }
    void paintGL() override {
        QPainter p(this);
        draw_(p);
    }
private:
    std::function<void(QPainter &)> draw_;
};

Canvas::Canvas(Document *doc, Settings *settings, const Theme *theme, QWidget *parent)
    : QWidget(parent), doc_(doc), settings_(settings), theme_(theme) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    view_settle_.setSingleShot(true);
    view_settle_.setInterval(120);
    connect(&view_settle_, &QTimer::timeout, this, [this] {
        dirty_ = true;
        updateCanvas();
    });
    const QString platform = QGuiApplication::platformName();
    if (platform != "offscreen" && platform != "minimal" &&
        qgetenv("FCAI_RENDERER") != "software") {
        surface_ = new CanvasSurface(this, [this](QPainter &p) { paint(p, true); });
        surface_->setGeometry(rect());
        surface_->lower();
    }
    sheet_ = new KeySheet(theme_, this);
}

QString Canvas::gpuRenderer() const { return surface_ ? surface_->renderer : QString(); }

void Canvas::updateCanvas() {
    // Schedule the GL child directly, without an extra parent repaint first.
    if (surface_) surface_->update();
    else QWidget::update();
}

int Canvas::grid() const {
    int g = settings_->getInt("grid");
    return g > 0 ? g : 5;
}

int Canvas::layer() const { return settings_->getInt("layer"); }

QColor Canvas::paper() const {
    QString p = settings_->get("paper");
    if (p == "white") return Qt::white;
    if (p == "theme" || !QColor::isValidColorName(p)) return theme_->bg;
    return QColor(p);
}

QString Canvas::num(double v) const {
    char buf[32];
    fc_fmt_num(buf, sizeof buf, v);
    return QString::fromLatin1(buf);
}

QPointF Canvas::toLogical(QPointF px) const {
    return QPointF((px.x() - ox_) / scale_, (px.y() - oy_) / scale_);
}

QPointF Canvas::toPixel(QPointF lg) const { return QPointF(lg.x() * scale_ + ox_, lg.y() * scale_ + oy_); }

QPointF Canvas::snap(QPointF lg) const {
    if (!settings_->getBool("snap")) return QPointF(std::round(lg.x()), std::round(lg.y()));
    int g = grid();
    return QPointF(std::round(lg.x() / g) * g, std::round(lg.y() / g) * g);
}

void Canvas::setTool(Tool t) {
    cancelOp();
    tool_ = t;
    if (t != Select) {
        sel_.clear();
        selected_handles_.clear();
        if (onSelectionChanged) onSelectionChanged();
    }
    setCursor(t == Select ? Qt::ArrowCursor : Qt::CrossCursor);
    status();
    updateCanvas();
}

void Canvas::setMacro(const QString &key) {
    macro_ = key;
    macro_orient_ = macro_mirror_ = 0;
    setTool(Macro);
    setFocus();
}

void Canvas::setSelection(const QList<int> &sel) {
    selected_handles_.clear();
    sel_.clear();
    for (int i : sel)
        if (i >= 0 && i < doc_->count() && !sel_.contains(i)) sel_ << i;
    if (onSelectionChanged) onSelectionChanged();
    status();
    updateCanvas();
}

void Canvas::selectAll() {
    if (tool_ != Select) setTool(Select);
    QList<int> all;
    for (int i = 0; i < doc_->count(); i++) all << i;
    setSelection(all);
}

void Canvas::fit() {
    QRectF b = doc_->boundsAll();
    if (doc_->count() == 0) b = QRectF(0, 0, 200, 150);
    b.adjust(-10, -10, 10, 10);
    double sx = width() / std::max(b.width(), 1.0), sy = height() / std::max(b.height(), 1.0);
    /* Fit must also contain drawings larger than the manual zoom range. */
    scale_ = std::min({sx, sy, 60.0});
    ox_ = width() / 2.0 - b.center().x() * scale_;
    oy_ = height() / 2.0 - b.center().y() * scale_;
    auto_fit_ = true;
    dirty_ = true;
    status();
    updateCanvas();
}

bool Canvas::fitRect(const QRectF &bounds) {
    if (!std::isfinite(bounds.left()) || !std::isfinite(bounds.top()) ||
        !std::isfinite(bounds.right()) || !std::isfinite(bounds.bottom()) ||
        std::abs(bounds.left()) > FC_MAX_COORD || std::abs(bounds.top()) > FC_MAX_COORD ||
        std::abs(bounds.right()) > FC_MAX_COORD || std::abs(bounds.bottom()) > FC_MAX_COORD ||
        bounds.width() <= 0 || bounds.height() <= 0 || width() <= 0 || height() <= 0)
        return false;
    const QRectF b = bounds.adjusted(-10, -10, 10, 10);
    scale_ = std::min({width() / b.width(), height() / b.height(), 60.0});
    ox_ = width() / 2.0 - b.center().x() * scale_;
    oy_ = height() / 2.0 - b.center().y() * scale_;
    auto_fit_ = false;
    dirty_ = true;
    status();
    updateCanvas();
    return true;
}

void Canvas::zoomBy(double f, QPointF at) {
    if (!std::isfinite(f) || f <= 0) return;
    auto_fit_ = false;
    double ns = std::clamp(scale_ * f, 0.25, 60.0);
    QPointF lg = toLogical(at);
    scale_ = ns;
    ox_ = at.x() - lg.x() * scale_;
    oy_ = at.y() - lg.y() * scale_;
    view_settle_.start();
    status();
    updateCanvas();
}

void Canvas::panBy(QPointF d) {
    if (d.isNull()) return;
    auto_fit_ = false;
    ox_ += d.x();
    oy_ += d.y();
    view_settle_.start();
    updateCanvas();
}

void Canvas::fitIfOutside() {
    if (doc_->count() == 0) return;
    QRectF b = doc_->boundsAll();
    QPointF lo = toPixel(b.topLeft()), hi = toPixel(b.bottomRight());
    if (lo.x() < 0 || lo.y() < 0 || hi.x() > width() || hi.y() > height()) fit();
}

void Canvas::refresh() {
    dirty_ = true;
    status();
    updateCanvas();
}

void Canvas::docChanged() {
    /* Undo, replacement and property edits can change compact handle indices. */
    if (op_ != Resizing && !editing_handles_) selected_handles_.clear();
    QList<int> keep;
    for (int i : sel_)
        if (i < doc_->count()) keep << i;
    if (keep != sel_) {
        sel_ = keep;
        if (onSelectionChanged) onSelectionChanged();
    }
    dirty_ = true;
    status();
    updateCanvas();
}

/* ---------- painting ---------- */

void Canvas::renderCache() {
    /* Overscan keeps newly exposed edges available during small gestures. */
    constexpr int margin = 192;
    qreal dpr = devicePixelRatioF();
    int w = std::max(1, (int)std::ceil((width() + 2 * margin) * dpr));
    int h = std::max(1, (int)std::ceil((height() + 2 * margin) * dpr));
    if (cache_.size() != QSize(w, h)) cache_ = QImage(w, h, QImage::Format_RGB888);
    cache_.setDevicePixelRatio(dpr);
    FcView v;
    memset(&v, 0, sizeof v);
    v.ox = (ox_ + margin) * dpr;
    v.oy = (oy_ + margin) * dpr;
    v.scale = scale_ * dpr;
    v.bg = toRgb(paper());
    v.grid = settings_->getBool("show_grid") ? grid() : 0;
    v.grid_major = 5;
    v.grid_rgb = toRgb(Theme::isDark(paper()) ? theme_->muted : QColor(190, 190, 200));
    v.tex = settings_->getBool("tex");
    uint32_t ink[FC_MAX_LAYERS];
    for (int i = 0; i < FC_MAX_LAYERS; i++) ink[i] = layerColor(i).rgb() & 0xFFFFFFu;
    v.layer_rgb = ink;
    fc_render_rgb(&doc_->drawing(), doc_->lib(), &v, cache_.bits(), w, h, (int)cache_.bytesPerLine());
    cache_origin_ = QPointF(ox_ + margin, oy_ + margin);
    cache_scale_ = scale_;
    dirty_ = false;
}

/* Outline of an expanded drawing; with origins, only what those primitives became. */
void Canvas::sketch(QPainter &p, const FcDrawing &flat, const QList<int> *origins) const {
    std::vector<char> keep;
    if (origins) {
        keep.assign(doc_->count(), 0);
        for (int i : *origins)
            if (i >= 0 && i < doc_->count()) keep[i] = 1;
    }
    for (int i = 0; i < flat.nprims; i++) {
        const FcPrim &q = flat.prims[i];
        if (origins && (q.origin < 0 || q.origin >= (int)keep.size() || !keep[q.origin])) continue;
        switch (q.kind) {
        case FC_LINE:
            p.drawLine(pt(q.u.line.a), pt(q.u.line.b));
            break;
        case FC_PCB_LINE:
            p.drawLine(pt(q.u.pcb_line.a), pt(q.u.pcb_line.b));
            break;
        case FC_BEZIER: {
            QPainterPath path(pt(q.u.bezier.p[0]));
            path.cubicTo(pt(q.u.bezier.p[1]), pt(q.u.bezier.p[2]), pt(q.u.bezier.p[3]));
            p.drawPath(path);
            break;
        }
        case FC_RECT:
            p.drawRect(QRectF(pt(q.u.box.a), pt(q.u.box.b)).normalized());
            break;
        case FC_OVAL:
            p.drawEllipse(QRectF(pt(q.u.box.a), pt(q.u.box.b)).normalized());
            break;
        case FC_POLY:
        case FC_CURVE: {
            QPolygonF poly;
            for (int k = 0; k < q.u.poly.n; k++) poly << pt(q.u.poly.pts[k]);
            if (q.kind == FC_POLY || q.u.poly.closed) p.drawPolygon(poly);
            else p.drawPolyline(poly);
            break;
        }
        case FC_CONN:
            p.drawEllipse(pt(q.u.conn.p), 1.0, 1.0);
            break;
        case FC_PCB_PAD:
            p.drawEllipse(pt(q.u.pad.p), q.u.pad.rx / 2, q.u.pad.ry / 2);
            break;
        case FC_IMAGE: {
            FcPoint corners[4]; fc_image_corners(&q, corners);
            QPolygonF poly; for (const auto &corner : corners) poly << pt(corner);
            p.drawPolygon(poly);
            break;
        }
        case FC_TEXT: {
            const FcLabel &l = q.u.text;
            FcRect box;
            fc_label_bounds(&l, settings_->getBool("tex"), &box);
            p.drawRect(QRectF(QPointF(box.min_x, box.min_y), QPointF(box.max_x, box.max_y)));
            break;
        }
        case FC_MACRO:
            p.drawEllipse(pt(q.u.macro.p), 2.0, 2.0);
            break;
        }
    }
}

void Canvas::sketchMacro(QPainter &p, QPointF at) const {
    FcDrawing one, flat;
    fc_drawing_init(&one);
    fc_drawing_init(&flat);
    QByteArray line = QString("MC %1 %2 %3 %4 %5\n")
                          .arg(num(at.x()), num(at.y()))
                          .arg(macro_orient_)
                          .arg(macro_mirror_)
                          .arg(macro_)
                          .toUtf8();
    fc_parse(&one, line.constData(), (size_t)line.size());
    fc_expand(&one, &flat, doc_->lib());
    sketch(p, flat);
    fc_drawing_free(&one);
    fc_drawing_free(&flat);
}

double Canvas::rulerLength() const {
    if (!ruler_active_) return 0.0;
    return std::hypot(ruler_b_.x() - ruler_a_.x(), ruler_b_.y() - ruler_a_.y());
}

/* Degrees counter-clockwise from +x, as on paper (logical y grows downwards). */
double Canvas::rulerAngle() const {
    return std::atan2(-(ruler_b_.y() - ruler_a_.y()), ruler_b_.x() - ruler_a_.x()) * 180.0 /
           3.14159265358979323846;
}

/* Measuring ruler, ported from FidoCadJS Ruler.ts: a green line with ticks,
 * plus a box with length in logical units, millimetres and degrees. Drawn in
 * widget space on top of the drawing. */
void Canvas::drawRuler(QPainter &p) const {
    if (!ruler_active_) return;
    QPointF a = toPixel(ruler_a_), b = toPixel(ruler_b_);
    double lg_len = rulerLength();
    if (lg_len <= 1e-9) return;

    const QColor green(0x00, 0xbb, 0x00);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(green, 1));
    p.setBrush(Qt::NoBrush);
    p.drawLine(a, b);

    /* Ticks: 1 logical unit when zoomed in, 5 normally, 10 when zoomed out. */
    double step = scale_ < 1.0 ? 10.0 : (scale_ > 5.0 ? 1.0 : 5.0);
    QPointF d = b - a;
    double px_len = std::hypot(d.x(), d.y());
    QPointF u = d / px_len, n(-u.y(), u.x());
    int j = 0;
    bool first = true;
    for (double t = 0; t <= lg_len + 1e-9; t += step) {
        double f = t / lg_len;
        QPointF c = a + d * f;
        bool major = first || j == 5;
        double half = major ? 10.0 : 5.0;
        if (major) {
            j = 0;
            first = false;
        }
        j++;
        p.drawLine(c + n * half, c - n * half);
    }

    /* Result box next to the end point: units, mm and angle. */
    QFont f = p.font();
    int fs = qMax(7, f.pointSize() - 2);
    f.setPointSize(fs);
    p.setFont(f);
    QFontMetrics fm(f);
    QString t1 = QString::number(lg_len, 'f', 2);
    QString t2 = QString::number(lg_len * 0.127, 'f', 2) + " mm";
    QString t3 = QString::number(rulerAngle(), 'f', 2) + "°";
    int bw = qMax(qMax(fm.horizontalAdvance(t1), fm.horizontalAdvance(t2)),
                  fm.horizontalAdvance(t3)) +
             8;
    int bh = fm.lineSpacing() * 3 + 4;
    QPointF at = b + QPointF(10, 0);
    at.setX(qBound(2.0, at.x(), qMax(2.0, width() - bw - 2.0)));
    at.setY(qBound(2.0, at.y(), qMax(2.0, height() - bh - 2.0)));
    QRect box(at.toPoint(), QSize(bw, bh));
    p.fillRect(box, Qt::white);
    p.setPen(green);
    p.drawRect(box);
    p.setPen(Qt::black);
    p.drawText(box.adjusted(4, 2, -2, 0), Qt::AlignTop | Qt::AlignLeft, t1);
    p.drawText(box.adjusted(4, 2 + fm.lineSpacing(), -2, 0), Qt::AlignTop | Qt::AlignLeft, t2);
    p.drawText(box.adjusted(4, 2 + 2 * fm.lineSpacing(), -2, 0), Qt::AlignTop | Qt::AlignLeft, t3);
    p.restore();
}

void Canvas::paintEvent(QPaintEvent *) {
    if (surface_) {
        surface_->update();
        return;
    }
    QPainter p(this);
    paint(p, false);
}

void Canvas::paintScene(QPainter &p) {
    if (dirty_) {
        std::array<QColor, FC_MAX_LAYERS> colors;
        for (int i = 0; i < FC_MAX_LAYERS; ++i) colors[i] = layerColor(i);
        scene_.rebuild(doc_->drawing(), doc_->lib(), colors, settings_->getBool("tex"),
                       scale_ * devicePixelRatioF());
        dirty_ = false;
    }
    p.fillRect(rect(), paper());
    double step = grid() * scale_;
    if (settings_->getBool("show_grid") && step * devicePixelRatioF() >= 4) {
        const qreal pixel = 1.0 / devicePixelRatioF();
        p.setPen(QPen(Theme::isDark(paper()) ? theme_->muted : QColor(190, 190, 200), pixel));
        QList<QPointF> points;
        auto firstX = static_cast<qint64>(std::floor(-ox_ / step));
        auto firstY = static_cast<qint64>(std::floor(-oy_ / step));
        for (qint64 y = firstY; y * step + oy_ < height(); ++y) {
            for (qint64 x = firstX; x * step + ox_ < width(); ++x) {
                QPointF dot(x * step + ox_, y * step + oy_);
                points << dot;
                if (x % 5 == 0 && y % 5 == 0)
                    points << dot + QPointF(pixel, 0) << dot - QPointF(pixel, 0)
                           << dot + QPointF(0, pixel) << dot - QPointF(0, pixel);
            }
        }
        p.drawPoints(points);
    }
    p.save();
    p.setTransform(QTransform(scale_, 0, 0, scale_, ox_, oy_));
    scene_.paint(p, QRectF(toLogical({0, 0}), toLogical({double(width()), double(height())})));
    p.restore();
}

void Canvas::paint(QPainter &p, bool gpu) {
    if (gpu) {
        paintScene(p);
    } else {
        auto cacheRect = [this] {
            double ratio = scale_ / cache_scale_;
            return QRectF(QPointF(ox_, oy_) - cache_origin_ * ratio,
                          QSizeF(cache_.size()) * (ratio / cache_.devicePixelRatio()));
        };
        /* View changes transform the existing raster. Content edits still redraw
         * immediately; exhausting the overscan also redraws to avoid blank edges. */
        if (dirty_ || cache_.isNull() || cache_.devicePixelRatio() != devicePixelRatioF() ||
            !cacheRect().contains(QRectF(rect()))) renderCache();
        p.setRenderHint(QPainter::SmoothPixmapTransform, scale_ != cache_scale_);
        p.drawImage(cacheRect(), cache_);
    }
    p.setRenderHint(QPainter::Antialiasing);

    QColor acc = theme_->accent;
    QTransform xf(scale_, 0, 0, scale_, ox_, oy_);

    /* selection: highlighted ink + dashed box */
    if (!sel_.isEmpty()) {
        p.save();
        p.setTransform(xf);
        QColor ink = acc;
        ink.setAlpha(170);
        QPen pen(ink, 2.5);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        FcDrawing flat;
        fc_drawing_init(&flat);
        fc_expand(&doc_->drawing(), &flat, doc_->lib());
        sketch(p, flat, &sel_);
        fc_drawing_free(&flat);
        p.restore();
        QRectF b = doc_->boundsOf(sel_);
        QRectF r(toPixel(b.topLeft()), toPixel(b.bottomRight()));
        QPen dash(acc, 1, Qt::DashLine);
        p.setPen(dash);
        p.drawRect(r.adjusted(-4, -4, 4, 4));
    }

    /* control-point handles for the selection (FidoCadJS-style resize grips) */
    if (!sel_.isEmpty() && !form_) {
        p.setPen(QPen(theme_->fg, 1));
        for (int i : sel_) {
            int n = doc_->handles(i);
            for (int h = 0; h < n; h++) {
                QPointF g = toPixel(doc_->handle(i, h));
                if (g.x() < -8 || g.y() < -8 || g.x() > width() + 8 || g.y() > height() + 8) continue;
                bool active = selected_handles_.contains(qMakePair(i, h));
                p.setBrush(selected_handles_.isEmpty() || active ? acc : paper());
                double radius = active ? 5 : 3.5;
                p.drawRect(QRectF(g.x() - radius, g.y() - radius, radius * 2, radius * 2));
            }
        }
        p.setBrush(Qt::NoBrush);
    }

    /* tool previews */
    p.setTransform(xf);
    QPen pen(acc, 1.5);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPointF c = cursor_lg_;
    if (has_cursor_ && tool_ != Select) {
        if ((tool_ == Line) && have_start_) p.drawLine(start_, c);
        if (tool_ == Rect && have_start_) {
            if (settings_->getBool("filled")) p.setBrush(QColor(acc.red(), acc.green(), acc.blue(), 60));
            p.drawRect(QRectF(start_, c).normalized());
        }
        if (tool_ == Oval && have_start_) {
            if (settings_->getBool("filled")) p.setBrush(QColor(acc.red(), acc.green(), acc.blue(), 60));
            p.drawEllipse(QRectF(start_, c).normalized());
        }
        if (tool_ == Macro && !macro_.isEmpty()) sketchMacro(p, c);
        if (tool_ == Dot) p.drawEllipse(c, 1.0, 1.0);
        /* snapped cursor cross */
        p.resetTransform();
        QPointF cp = toPixel(c);
        p.drawLine(cp + QPointF(-6, 0), cp + QPointF(6, 0));
        p.drawLine(cp + QPointF(0, -6), cp + QPointF(0, 6));
    }
    p.resetTransform();
    if (op_ == Banding) {
        QRectF r(press_px_, toPixel(cursor_lg_));
        QColor fill = acc;
        fill.setAlpha(40);
        p.setPen(QPen(acc, 1, Qt::DashLine));
        p.setBrush(fill);
        p.drawRect(r.normalized());
    }
    if (ruler_active_) drawRuler(p);
}

void Canvas::resizeEvent(QResizeEvent *) {
    if (auto_fit_) fit();
    if (surface_) surface_->setGeometry(rect());
    sheet_->setGeometry(rect());
    dirty_ = true;
}

/* ---------- mouse ---------- */

/* Nearest selection control point within 8 px, or -1; *handle_out gets its index. */
int Canvas::handleAt(QPointF px, int *handle_out) const {
    double best = 8.0;
    int best_prim = -1, best_h = -1;
    for (int i : sel_) {
        int n = doc_->handles(i);
        for (int h = 0; h < n; h++) {
            QPointF g = toPixel(doc_->handle(i, h));
            double d = std::hypot(g.x() - px.x(), g.y() - px.y());
            if (d <= best) {
                best = d;
                best_prim = i;
                best_h = h;
            }
        }
    }
    if (handle_out) *handle_out = best_h;
    return best_prim;
}

void Canvas::snapshotHandles() {
    handle_starts_.clear();
    for (const auto &h : selected_handles_) handle_starts_ << doc_->handle(h.first, h.second);
}

void Canvas::moveHandles(QPointF delta) {
    /* Clamp the whole group together so labels keep their spacing at edges. */
    double minX = -FC_MAX_COORD, minY = -FC_MAX_COORD;
    double maxX = FC_MAX_COORD, maxY = FC_MAX_COORD;
    for (QPointF p : handle_starts_) {
        minX = std::max(minX, -p.x());
        minY = std::max(minY, -p.y());
        maxX = std::min(maxX, FC_MAX_COORD - p.x());
        maxY = std::min(maxY, FC_MAX_COORD - p.y());
    }
    delta.setX(std::clamp(delta.x(), minX, maxX));
    delta.setY(std::clamp(delta.y(), minY, maxY));
    for (int j = 0; j < selected_handles_.size(); j++) {
        const auto h = selected_handles_[j];
        QPointF p = handle_starts_[j] + delta;
        doc_->setHandle(h.first, h.second, p.x(), p.y());
    }
}

QColor Canvas::layerColor(int l) const {
    if (l < 0 || l >= FC_MAX_LAYERS) l = 0;
    return theme_->ink(doc_->drawing().layers[l].rgb, paper());
}

void Canvas::status() {
    if (!onStatus) return;
    QString s = toolName(tool_).toHtmlEscaped();
    if (tool_ == Macro && !macro_.isEmpty()) s += " " + macro_.toHtmlEscaped();
    if (has_cursor_) s += QString(" · x %1 y %2").arg(num(cursor_lg_.x()), num(cursor_lg_.y()));
    if (ruler_active_)
        s += QString(" · ruler %1 u  %2 mm  %3°")
                 .arg(QString::number(rulerLength(), 'f', 2))
                 .arg(QString::number(rulerLength() * 0.127, 'f', 2))
                 .arg(QString::number(rulerAngle(), 'f', 2));
    s += QString(" · zoom %1%").arg((int)std::round(scale_ * 25));
    s += QString(" · <span style=\"color:%1\">layer %2</span>")
             .arg(layerColor(layer()).name(), QString::number(layer()));
    if (!sel_.isEmpty()) s += QString(" · selected %1").arg(sel_.size());
    if (!selected_handles_.isEmpty()) s += QString(" · handles %1").arg(selected_handles_.size());
    onStatus(s);
}

void Canvas::cancelOp() {
    if (op_ == Moving || op_ == Resizing) doc_->endChange();
    op_ = None;
    have_start_ = false;
    updateCanvas();
}

void Canvas::finishLine() {
    have_start_ = false;
    updateCanvas();
}

void Canvas::place(QPointF a) {
    int l = layer();
    QString x = num(a.x()), y = num(a.y());
    switch (tool_) {
    case Dot:
        doc_->append(QString("SA %1 %2 %3").arg(x, y).arg(l));
        break;
    case Text: {
        if (form_) form_->cancel();
        auto *f = new InlineForm(this, "New text");
        QLineEdit *t = f->addText("Text", QString(), "$...$ for math");
        f->onApply = [this, t, x, y, l]() -> QString {
            if (!t->text().trimmed().isEmpty())
                doc_->append(QString("TY %1 %2 4 3 0 0 %3 * %4").arg(x, y).arg(l).arg(t->text().trimmed()));
            return QString();
        };
        f->onClosed = [this] { form_ = nullptr; };
        form_ = f;
        f->popup(toPixel(a).toPoint() + QPoint(8, 8));
        break;
    }
    case Macro:
        if (!macro_.isEmpty())
            doc_->append(QString("MC %1 %2 %3 %4 %5")
                             .arg(x, y)
                             .arg(macro_orient_)
                             .arg(macro_mirror_)
                             .arg(macro_));
        break;
    default:
        break;
    }
}

/* Shift locks the segment being drawn to horizontal or vertical. */
QPointF Canvas::lineEnd(QPointF s, Qt::KeyboardModifiers mods) const {
    if (tool_ != Line || !have_start_ || !(mods & Qt::ShiftModifier)) return s;
    if (std::abs(s.x() - start_.x()) > std::abs(s.y() - start_.y())) s.setY(start_.y());
    else s.setX(start_.x());
    return s;
}

/* Click selection: with add (Ctrl/Shift) toggle the primitive under lg,
 * otherwise select it, or clear on empty space. Returns it, or -1. */
int Canvas::selectAt(QPointF lg, bool add) {
    int h = doc_->hit(lg.x(), lg.y(), 5.0 / scale_);
    if (h >= 0 && add) {
        QList<int> s2 = sel_;
        if (s2.contains(h)) s2.removeAll(h);
        else s2 << h;
        setSelection(s2);
    } else if (h >= 0 && !sel_.contains(h)) {
        setSelection({h});
    } else if (h < 0 && !add) {
        setSelection({});
    }
    return h;
}

void Canvas::addBox(QPointF a, QPointF b) {
    bool filled = settings_->getBool("filled");
    QString code = tool_ == Rect ? (filled ? "RP" : "RV") : (filled ? "EP" : "EV");
    doc_->append(QString("%1 %2 %3 %4 %5 %6")
                     .arg(code, num(a.x()), num(a.y()), num(b.x()), num(b.y()))
                     .arg(layer()));
}

/* A plain left click with a drawing tool at the snapped point s. */
void Canvas::toolClick(QPointF s, Qt::KeyboardModifiers mods) {
    switch (tool_) {
    case Select:
        break;
    case Line:
        if (have_start_) {
            QPointF b = lineEnd(s, mods);
            if (b != start_)
                doc_->append(QString("LI %1 %2 %3 %4 %5")
                                 .arg(num(start_.x()), num(start_.y()), num(b.x()), num(b.y()))
                                 .arg(layer()));
            start_ = b;
        } else {
            start_ = s;
            have_start_ = true;
        }
        break;
    case Rect:
    case Oval:
        if (have_start_) {
            if (s != start_) addBox(start_, s);
            have_start_ = false;
        } else {
            start_ = s;
            have_start_ = true;
            op_ = Drawing; /* a drag finishes the box on release */
        }
        break;
    case Dot:
    case Text:
    case Macro:
        place(s);
        break;
    }
}

void Canvas::mousePressEvent(QMouseEvent *e) {
    if (form_) form_->apply(); /* clicking away keeps what was typed */
    /* a new press clears a measurement left on screen from a previous drag */
    if (ruler_active_) ruler_active_ = false;
    setFocus();
    press_px_ = e->position();
    press_lg_ = toLogical(e->position());
    QPointF s = snap(press_lg_);
    has_cursor_ = true;
    cursor_lg_ = lineEnd(s, e->modifiers());

    if (e->button() == Qt::MiddleButton) {
        op_ = Panning;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        /* right-button drag measures; a plain right-click keeps its old
         * meaning, decided when the button is released */
        ruler_dragged_ = false;
        ruler_a_ = ruler_b_ = press_lg_;
        op_ = RulerArmed;
        return;
    }
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier) &&
        !(tool_ == Line && have_start_)) {
        /* FidoCadJS: Shift + left-drag measures; a plain Shift-click falls
         * back to what the tool would have done (decided on release) */
        ruler_dragged_ = false;
        ruler_a_ = ruler_b_ = press_lg_;
        op_ = RulerArmed;
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    if (tool_ != Select) {
        toolClick(s, e->modifiers());
        updateCanvas();
        return;
    }
    int hh = -1;
    int hp = handleAt(press_px_, &hh);
    if (hp >= 0) {
        auto handle = qMakePair(hp, hh);
        if (e->modifiers() & Qt::ControlModifier) {
            if (selected_handles_.contains(handle)) selected_handles_.removeAll(handle);
            else selected_handles_ << handle;
            status();
            updateCanvas();
            return;
        }
        if (!selected_handles_.contains(handle)) selected_handles_ = {handle};
        snapshotHandles();
        drag_prim_ = hp;
        drag_handle_ = hh;
        doc_->beginChange();
        op_ = Resizing;
        setCursor(Qt::SizeAllCursor);
        return;
    }
    int h = selectAt(press_lg_, e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
    if (h < 0) {
        op_ = Banding;
    } else {
        selected_handles_.clear();
        if (sel_.contains(h)) {
            op_ = Moving;
            moved_ = QPointF(0, 0);
            doc_->beginChange();
        }
    }
    updateCanvas();
}

void Canvas::mouseMoveEvent(QMouseEvent *e) {
    QPointF lg = toLogical(e->position());
    has_cursor_ = true;
    QPointF s = lineEnd(snap(lg), e->modifiers());
    cursor_lg_ = op_ == Banding ? lg : s;

    if (op_ == RulerArmed) {
        if ((e->position() - press_px_).manhattanLength() > 3) ruler_dragged_ = true;
        if (ruler_dragged_) {
            ruler_b_ = lg; /* unsnapped, like FidoCadJS */
            ruler_active_ = true;
            updateCanvas();
        }
        status();
        return;
    }
    if (op_ == None) {
        int hh = -1;
        bool over = handleAt(e->position(), &hh) >= 0;
        setCursor(tool_ == Select ? (over ? Qt::SizeAllCursor : Qt::ArrowCursor) : Qt::CrossCursor);
    } else if (op_ == Resizing) {
        int anchor = selected_handles_.indexOf(qMakePair(drag_prim_, drag_handle_));
        if (anchor >= 0) moveHandles(s - handle_starts_[anchor]);
        cursor_lg_ = s;
        updateCanvas();
    } else if (op_ == Panning) {
        panBy(e->position() - press_px_);
        press_px_ = e->position();
    } else if (op_ == Moving) {
        QPointF want = snap(lg) - snap(press_lg_);
        QPointF step = want - moved_;
        if (step != QPointF(0, 0)) {
            doc_->translateLive(sel_, step.x(), step.y());
            moved_ = want;
        }
    }
    status();
    updateCanvas();
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    /* A release carries its own position; platforms may coalesce the final
     * move or deliver no move at all during a short drag. Apply it before
     * ending the operation and its undo transaction. */
    if (op_ == Moving || op_ == Resizing || op_ == Panning || op_ == RulerArmed)
        mouseMoveEvent(e);
    QPointF lg = toLogical(e->position());
    if (op_ == RulerArmed) {
        op_ = None;
        if (ruler_dragged_) {
            /* leave the measurement on screen until the next press */
            updateCanvas();
            return;
        }
        ruler_active_ = false;
        if (e->button() == Qt::RightButton) {
            if (tool_ == Line && have_start_) finishLine();
            else if (tool_ != Select) setTool(Select);
            return;
        }
        if (e->button() == Qt::LeftButton) {
            /* plain Shift-click: what a click does, without a drag to follow */
            if (tool_ == Select) selectAt(lg, e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
            else toolClick(snap(lg), e->modifiers());
            op_ = None;
            updateCanvas();
        }
        return;
    }
    if (op_ == Panning) {
        op_ = None;
        setCursor(tool_ == Select ? Qt::ArrowCursor : Qt::CrossCursor);
        return;
    }
    if (op_ == Moving) {
        op_ = None;
        doc_->endChange();
        return;
    }
    if (op_ == Resizing) {
        doc_->endChange();
        op_ = None;
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (op_ == Banding) {
        op_ = None;
        if ((e->position() - press_px_).manhattanLength() > 4) {
            QRectF band = QRectF(press_lg_, lg).normalized();
            QList<int> hit = doc_->inRect(band);
            QList<int> s2 = (e->modifiers() & Qt::ControlModifier) ? sel_ : QList<int>();
            if (hit.isEmpty()) {
                /* A box around just labels/control points selects those
                 * handles rather than their complete parent components. */
                auto handles = (e->modifiers() & Qt::ControlModifier)
                                   ? selected_handles_ : QList<QPair<int, int>>();
                for (int i = 0; i < doc_->count(); i++) {
                    for (int h = 0; h < doc_->handles(i); h++) {
                        auto handle = qMakePair(i, h);
                        if (!band.contains(doc_->handle(i, h))) continue;
                        if (!handles.contains(handle)) handles << handle;
                        if (!s2.contains(i)) s2 << i;
                    }
                }
                setSelection(s2);
                selected_handles_ = handles;
                status();
            } else {
                for (int i : hit)
                    if (!s2.contains(i)) s2 << i;
                setSelection(s2);
            }
        }
        updateCanvas();
        return;
    }
    if (op_ == Drawing) {
        op_ = None;
        /* dragged a rect/oval: finish on release; a plain click waits for the 2nd corner */
        QPointF s = snap(lg);
        if ((e->position() - press_px_).manhattanLength() > 6 && s != start_) {
            addBox(start_, s);
            have_start_ = false;
        }
    }
    updateCanvas();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    /* Qt replaces the second press with a double-click event. Drawing tools
     * must consume that position too, otherwise fast clicks drop a vertex or
     * component. Only Select uses the double-click for properties. */
    if (e->button() != Qt::LeftButton || tool_ != Select) {
        mousePressEvent(e);
        if (e->button() == Qt::LeftButton && tool_ == Line)
            finishLine();
        return;
    }
    QPointF lg = toLogical(e->position());
    int h = doc_->hit(lg.x(), lg.y(), 5.0 / scale_);
    if (h < 0) return;
    setSelection({h});
    editSelection();
}

void Canvas::wheelEvent(QWheelEvent *e) {
    QPoint pd = e->pixelDelta();
    if (!pd.isNull() && !(e->modifiers() & Qt::ControlModifier)) {
        /* touchpad two-finger scroll pans */
        panBy(pd);
        return;
    }
    /* Ctrl+scroll zooms; some stacks deliver pinch that way too */
    double steps = !pd.isNull() ? pd.y() / 100.0 : e->angleDelta().y() / 120.0;
    if (steps != 0) zoomBy(std::pow(1.2, steps), e->position());
}

bool Canvas::event(QEvent *e) {
    if (e->type() != QEvent::NativeGesture) return QWidget::event(e);
    /* Wayland (zwp_pointer_gestures), X11 XInput 2.4 and macOS deliver
       touchpad pinch as native gestures; value() is the incremental scale */
    auto *g = static_cast<QNativeGestureEvent *>(e);
    switch (g->gestureType()) {
    case Qt::ZoomNativeGesture:
        if (g->value() > -1.0) zoomBy(1.0 + g->value(), g->position());
        break;
    case Qt::SmartZoomNativeGesture: /* macOS two-finger double tap */
        fit();
        break;
    case Qt::PanNativeGesture:
        panBy(g->delta()); /* swipe: content follows the fingers */
        break;
    default:
        break;
    }
    g->accept();
    return true;
}

void Canvas::leaveEvent(QEvent *) {
    has_cursor_ = false;
    updateCanvas();
}

/* ---------- keyboard-level actions (bound as QActions by the window) ---------- */

void Canvas::keyPressEvent(QKeyEvent *e) {
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (!mods) {
        bool shift = e->modifiers() & Qt::ShiftModifier;
        switch (e->key()) {
        case Qt::Key_K: toggleCheatsheet(); return;
        case Qt::Key_I:
            if (shift) { importImage(); return; }
            if (onToggleBar) onToggleBar();
            return;
        case Qt::Key_S: setTool(Select); return;
        case Qt::Key_L: setTool(Line); return;
        case Qt::Key_B: setTool(Rect); return;
        case Qt::Key_E: setTool(Oval); return;
        case Qt::Key_J: setTool(Dot); return;
        case Qt::Key_T: setTool(Text); return;
        case Qt::Key_R: rotateSelection(); return;
        case Qt::Key_M: mirrorSelection(); return;
        case Qt::Key_P:
        case Qt::Key_F2: editSelection(); return;
        case Qt::Key_Delete:
        case Qt::Key_Backspace: deleteSelection(); return;
        case Qt::Key_BracketLeft: cycleLayer(-1); return;
        case Qt::Key_BracketRight: cycleLayer(1); return;
        case Qt::Key_F: flipSetting("filled"); return;
        case Qt::Key_G: flipSetting(shift ? "snap" : "show_grid"); return;
        case Qt::Key_0: fit(); return;
        case Qt::Key_Space: fit(); return;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomBy(1.25, rect().center()); return;
        case Qt::Key_Minus: zoomBy(0.8, rect().center()); return;
        default: break;
        }
    }
    if (e->key() == Qt::Key_Escape) {
        if (ruler_active_) { ruler_active_ = false; updateCanvas(); return; }
        if (tool_ == Line && have_start_) finishLine();
        else if (tool_ != Select) setTool(Select);
        else setSelection({});
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !mods) {
        if (tool_ == Line && have_start_) finishLine();
        return;
    }
    int g = grid();
    switch (e->key()) {
    case Qt::Key_Left: nudge(-g, 0); return;
    case Qt::Key_Right: nudge(g, 0); return;
    case Qt::Key_Up: nudge(0, -g); return;
    case Qt::Key_Down: nudge(0, g); return;
    default: break;
    }
    QWidget::keyPressEvent(e);
}

void Canvas::rotateSelection() {
    if (tool_ == Macro) {
        macro_orient_ = (macro_orient_ + 1) & 3;
        updateCanvas();
        return;
    }
    doc_->rotate(sel_);
}

void Canvas::mirrorSelection() {
    if (tool_ == Macro) {
        macro_mirror_ = !macro_mirror_;
        updateCanvas();
        return;
    }
    doc_->mirror(sel_);
}

void Canvas::deleteSelection() {
    QList<int> s = sel_;
    sel_.clear();
    doc_->remove(s);
    if (onSelectionChanged) onSelectionChanged();
}

void Canvas::duplicateSelection() {
    if (sel_.isEmpty()) return;
    setSelection(doc_->duplicate(sel_, grid(), grid()));
}

void Canvas::copySelection() {
    if (sel_.isEmpty()) return;
    QString text = "[FIDOCAD]\n";
    QList<int> s = sel_;
    std::sort(s.begin(), s.end());
    for (int i : s) text += doc_->primFcd(i) + "\n";
    QApplication::clipboard()->setText(text);
}

void Canvas::paste() {
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime) return;
    QString text = mime->text();
    if (!text.trimmed().startsWith("[FIDOCAD]")) {
        QString error;
        if (mime->hasUrls()) {
            doc_->beginChange();
            QList<int> added;
            QPointF at = imagePosition();
            for (const auto &url : mime->urls()) if (url.isLocalFile()) {
                if (importImageFile(url.toLocalFile(), at, &error)) {
                    added.append(selection()); at += QPointF(10,10);
                }
            }
            if (!added.isEmpty()) setSelection(added);
            doc_->endChange();
            if (!error.isEmpty() && onStatus) onStatus(error);
            return;
        }
        if (mime->hasImage()) {
            QImage image = qvariant_cast<QImage>(mime->imageData());
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            if (image.save(&buffer, "PNG")) importImageBytes(bytes, imagePosition(), &error);
            if (!error.isEmpty() && onStatus) onStatus(error);
            return;
        }
    }
    if (text.trimmed().isEmpty()) return;
    doc_->beginChange();
    QList<int> added = doc_->append(text);
    if (!added.isEmpty() && has_cursor_) {
        QRectF b = doc_->boundsOf(added);
        QPointF d = snap(cursor_lg_) - snap(b.topLeft());
        doc_->translateLive(added, d.x(), d.y());
    }
    doc_->endChange();
    if (tool_ != Select) setTool(Select);
    setSelection(added);
}

QPointF Canvas::imagePosition() const {
    QPointF at = snap(has_cursor_ ? cursor_lg_ : toLogical(rect().center()));
    return QPointF(qMax(0.,at.x()), qMax(0.,at.y()));
}

bool Canvas::importImageBytes(const QByteArray &bytes, QPointF at, QString *error) {
    int i = doc_->addImage(bytes, at, layer(), error);
    if (i < 0) return false;
    setTool(Select); setSelection({i}); fitIfOutside();
    if (onStatus) onStatus("Image embedded · drag to move · corner handles resize · P for properties");
    return true;
}

bool Canvas::importImageFile(const QString &path, QPointF at, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "Cannot open image: " + file.errorString();
        return false;
    }
    if (file.size() > FC_IMAGE_MAX_BYTES) {
        if (error) *error = "Image exceeds 24 MiB.";
        return false;
    }
    return importImageBytes(file.read(FC_IMAGE_MAX_BYTES + 1), at, error);
}

void Canvas::importImage() {
    cancelOp();
    if (form_) form_->cancel();
    auto *f = new InlineForm(this, "Import image · PNG / JPEG");
    auto *path = f->addText("File", QString(), "/path/to/image.png");
    auto *model = new QFileSystemModel(f);
    model->setRootPath(QString());
    auto *completer = new QCompleter(model, f); path->setCompleter(completer);
    const QPointF at = imagePosition();
    f->onApply = [this,path,at]() -> QString {
        QString error;
        return importImageFile(path->text().trimmed(), at, &error) ? QString() : error;
    };
    f->onClosed = [this] { form_ = nullptr; };
    form_ = f; f->popup(rect().center());
}

void Canvas::dragEnterEvent(QDragEnterEvent *event) {
    const auto *mime = event->mimeData();
    if (mime->hasImage()) { event->acceptProposedAction(); return; }
    for (const auto &url : mime->urls()) {
        const QString path = url.toLocalFile().toLower();
        if (url.isLocalFile() && (path.endsWith(".png") || path.endsWith(".jpg") || path.endsWith(".jpeg"))) {
            event->acceptProposedAction(); return;
        }
    }
}

void Canvas::dropEvent(QDropEvent *event) {
    if (form_) form_->cancel();
    const auto *mime = event->mimeData();
    QPointF at = snap(toLogical(event->position()));
    at = QPointF(qMax(0.,at.x()), qMax(0.,at.y()));
    QString error; bool imported = false;
    doc_->beginChange();
    if (mime->hasUrls()) {
        QList<int> added;
        for (const auto &url : mime->urls()) if (url.isLocalFile()) {
            if (importImageFile(url.toLocalFile(), at, &error)) {
                added.append(selection()); imported = true; at += QPointF(10,10);
            }
        }
        if (!added.isEmpty()) setSelection(added);
    } else if (mime->hasImage()) {
        QImage image = qvariant_cast<QImage>(mime->imageData());
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (image.save(&buffer,"PNG")) imported = importImageBytes(bytes, at, &error);
    }
    doc_->endChange();
    if (!error.isEmpty() && onStatus) onStatus(error);
    if (imported) event->acceptProposedAction();
}

void Canvas::nudge(int dx, int dy) {
    if (sel_.isEmpty()) return;
    if (!selected_handles_.isEmpty()) {
        snapshotHandles();
        editing_handles_ = true;
        doc_->beginChange();
        moveHandles(QPointF(dx, dy));
        doc_->endChange();
        editing_handles_ = false;
        return;
    }
    doc_->translate(sel_, dx, dy);
}

void Canvas::editSelection() {
    if (sel_.size() != 1) return;
    if (form_) form_->cancel();
    int i = sel_[0];
    InlineForm *f = editProperties(this, doc_, i);
    if (!f) return;
    f->onClosed = [this] { form_ = nullptr; };
    form_ = f;
    QRectF b = doc_->boundsOf({i});
    f->popup(toPixel(QPointF(b.right(), b.bottom())).toPoint() + QPoint(12, 12));
}

void Canvas::cycleLayer(int step) {
    int l = (settings_->getInt("layer") + step + FC_MAX_LAYERS) % FC_MAX_LAYERS;
    settings_->set("layer", QString::number(l));
    if (!sel_.isEmpty()) doc_->setLayer(sel_, l);
    if (onSettingChanged) onSettingChanged("layer");
    updateCanvas();
}

void Canvas::flipSetting(const QString &key) {
    settings_->set(key, settings_->getBool(key) ? "0" : "1");
    if (onSettingChanged) onSettingChanged(key);
    refresh();
}

void Canvas::toggleCheatsheet() {
    if (sheet_->isVisible()) sheet_->dismiss();
    else sheet_->open();
}
