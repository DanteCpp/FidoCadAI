#include "canvas.h"
#include "props.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <cmath>
#include <cstring>

namespace {

double luma(uint32_t rgb) {
    return (0.299 * ((rgb >> 16) & 255) + 0.587 * ((rgb >> 8) & 255) + 0.114 * (rgb & 255)) / 255.0;
}

uint32_t toRgb(const QColor &c) { return (uint32_t)c.rgb() & 0xFFFFFFu; }

/* Shallow copy of the drawing with layer colours adjusted for the paper:
 * on dark paper, near-black layers (FidoCad's default circuit layer) take
 * the theme foreground so they stay visible. Saturated colours (red, green)
 * are kept — their luma is low but they read fine on dark paper. */
FcDrawing paperView(const FcDrawing &d, const QColor &paper, const QColor &fg) {
    FcDrawing v = d;
    if (luma(toRgb(paper)) < 0.5)
        for (int i = 0; i < FC_MAX_LAYERS; i++)
            if (luma(v.layers[i].rgb) < 0.2) v.layers[i].rgb = toRgb(fg);
    return v;
}

void oneView(const FcDrawing &d, int i, FcDrawing *view) {
    memset(view, 0, sizeof *view);
    view->prims = const_cast<FcPrim *>(&d.prims[i]);
    view->nprims = view->cap_prims = 1;
    view->cfg = d.cfg;
    memcpy(view->layers, d.layers, sizeof view->layers);
}

QPointF pt(const FcPoint &p) { return QPointF(p.x, p.y); }

} // namespace

Canvas::Canvas(Document *doc, Settings *settings, const Theme *theme, QWidget *parent)
    : QWidget(parent), doc_(doc), settings_(settings), theme_(theme) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    loadBackground();
    cheats_bar_ = new QScrollBar(Qt::Vertical, this);
    cheats_bar_->setObjectName("cheatsheetScrollBar");
    cheats_bar_->setFocusPolicy(Qt::NoFocus);
    cheats_bar_->hide();
    connect(cheats_bar_, &QScrollBar::valueChanged, this, [this](int value) {
        cheats_scroll_ = value;
        update();
    });
}

QString Canvas::toolName(Tool t) {
    switch (t) {
    case Select: return "select";
    case Line: return "line";
    case Rect: return "rectangle";
    case Oval: return "ellipse";
    case Dot: return "connection";
    case Text: return "text";
    case Macro: return "symbol";
    }
    return QString();
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
    if (onToolChanged) onToolChanged();
    status();
    update();
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
    update();
}

void Canvas::selectAll() {
    if (tool_ != Select) setTool(Select);
    QList<int> all;
    for (int i = 0; i < doc_->count(); i++) all << i;
    setSelection(all);
}

void Canvas::fit() {
    QRectF b = doc_->boundsAll(), pic = backgroundRect();
    if (doc_->count() == 0) b = pic.isEmpty() ? QRectF(0, 0, 200, 150) : pic;
    else if (!pic.isEmpty()) /* not united(): that drops a zero-size drawing */
        b = QRectF(QPointF(std::min(b.left(), pic.left()), std::min(b.top(), pic.top())),
                   QPointF(std::max(b.right(), pic.right()), std::max(b.bottom(), pic.bottom())));
    b.adjust(-10, -10, 10, 10);
    double sx = width() / std::max(b.width(), 1.0), sy = height() / std::max(b.height(), 1.0);
    /* Fit must also contain drawings larger than the manual zoom range. */
    scale_ = std::min({sx, sy, 60.0});
    ox_ = width() / 2.0 - b.center().x() * scale_;
    oy_ = height() / 2.0 - b.center().y() * scale_;
    auto_fit_ = true;
    dirty_ = true;
    status();
    update();
}

void Canvas::zoomBy(double f, QPointF at) {
    auto_fit_ = false;
    double ns = std::clamp(scale_ * f, 0.25, 60.0);
    QPointF lg = toLogical(at);
    scale_ = ns;
    ox_ = at.x() - lg.x() * scale_;
    oy_ = at.y() - lg.y() * scale_;
    dirty_ = true;
    status();
    update();
}

void Canvas::panBy(QPointF d) {
    if (d.isNull()) return;
    auto_fit_ = false;
    ox_ += d.x();
    oy_ += d.y();
    dirty_ = true;
    update();
}

void Canvas::fitIfOutside() {
    if (doc_->count() == 0) return;
    QRectF b = doc_->boundsAll();
    QPointF lo = toPixel(b.topLeft()), hi = toPixel(b.bottomRight());
    if (lo.x() < 0 || lo.y() < 0 || hi.x() > width() || hi.y() > height()) fit();
}

void Canvas::refresh() {
    loadBackground();
    dirty_ = true;
    status();
    update();
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
    update();
}

/* ---------- painting ---------- */

void Canvas::renderCache() {
    qreal dpr = devicePixelRatioF();
    int w = std::max(1, (int)std::ceil(width() * dpr)), h = std::max(1, (int)std::ceil(height() * dpr));
    if (cache_.size() != QSize(w, h)) cache_ = QImage(w, h, QImage::Format_RGB888);
    cache_.setDevicePixelRatio(dpr);
    FcView v;
    memset(&v, 0, sizeof v);
    v.ox = ox_ * dpr;
    v.oy = oy_ * dpr;
    v.scale = scale_ * dpr;
    v.bg = toRgb(paper());
    v.grid = settings_->getBool("show_grid") ? grid() : 0;
    v.grid_major = 5;
    QColor gc = paper().lightnessF() < 0.5 ? theme_->muted : QColor(190, 190, 200);
    v.grid_rgb = toRgb(gc);
    v.tex = settings_->getBool("tex");
    QRectF pic = backgroundRect();
    if (!pic.isEmpty()) {
        /* paper, then the tracing picture, then grid and drawing on top */
        cache_.fill(paper());
        QPainter p(&cache_);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setOpacity(std::clamp(settings_->getInt("bg_image_opacity"), 0, 100) / 100.0);
        p.drawImage(QRectF(toPixel(pic.topLeft()), toPixel(pic.bottomRight())), bg_img_);
        v.keep_bg = 1;
    }
    FcDrawing pv = paperView(doc_->drawing(), paper(), theme_->fg);
    fc_render_rgb(&pv, doc_->lib(), &v, cache_.bits(), w, h, (int)cache_.bytesPerLine());
    dirty_ = false;
}

void Canvas::loadBackground() {
    QString path = settings_->filePath("bg_image");
    QDateTime mtime = path.isEmpty() ? QDateTime() : QFileInfo(path).lastModified();
    if (path == bg_path_ && mtime == bg_mtime_) return;
    bg_path_ = path;
    bg_mtime_ = mtime;
    bg_img_ = QImage();
    bg_error_.clear();
    if (path.isEmpty()) return;
    QImageReader reader(path);
    reader.setAutoTransform(true); /* phone photos: honour the EXIF rotation */
    bg_img_ = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (bg_img_.isNull()) bg_error_ = QString("cannot read bg_image %1: %2").arg(path, reader.errorString());
}

QRectF Canvas::backgroundRect() const {
    if (bg_img_.isNull() || !settings_->getBool("show_bg_image")) return QRectF();
    double w = settings_->getDouble("bg_image_width");
    if (w <= 0) w = bg_img_.width();
    return QRectF(settings_->getDouble("bg_image_x"), settings_->getDouble("bg_image_y"), w,
                  w * bg_img_.height() / bg_img_.width());
}

QImage Canvas::renderImage(int w, int h) const {
    QImage img(w, h, QImage::Format_RGB888);
    QRectF b = doc_->boundsAll();
    if (doc_->count() == 0) b = QRectF(0, 0, 100, 100);
    b.adjust(-5, -5, 5, 5);
    FcView v;
    memset(&v, 0, sizeof v);
    v.scale = std::min(w / b.width(), h / b.height());
    v.ox = w / 2.0 - b.center().x() * v.scale;
    v.oy = h / 2.0 - b.center().y() * v.scale;
    v.bg = 0xFFFFFF;
    v.tex = settings_->getBool("tex");
    fc_render_rgb(&doc_->drawing(), doc_->lib(), &v, img.bits(), w, h, (int)img.bytesPerLine());
    return img;
}

void Canvas::sketch(QPainter &p, const FcDrawing &flat) const {
    for (int i = 0; i < flat.nprims; i++) {
        const FcPrim &q = flat.prims[i];
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
            const FcPoint *pts = q.kind == FC_POLY ? q.u.poly.pts : q.u.curve.pts;
            int n = q.kind == FC_POLY ? q.u.poly.n : q.u.curve.n;
            QPolygonF poly;
            for (int k = 0; k < n; k++) poly << pt(pts[k]);
            if (q.kind == FC_POLY || q.u.curve.closed) p.drawPolygon(poly);
            else p.drawPolyline(poly);
            break;
        }
        case FC_CONN:
            p.drawEllipse(pt(q.u.conn.p), 1.0, 1.0);
            break;
        case FC_PCB_PAD:
            p.drawEllipse(pt(q.u.pad.p), q.u.pad.rx / 2, q.u.pad.ry / 2);
            break;
        case FC_TEXT: {
            const FcLabel &l = q.u.text;
            double w = (l.text ? strlen(l.text) : 1) * (l.size_x > 0 ? l.size_x : 3);
            p.drawRect(QRectF(l.p.x, l.p.y, w, l.size_y > 0 ? l.size_y : 4));
            break;
        }
        case FC_MACRO:
            p.drawEllipse(pt(q.u.macro.p), 2.0, 2.0);
            break;
        }
    }
}

void Canvas::sketchIndex(QPainter &p, int i) const {
    FcDrawing view, flat;
    oneView(doc_->drawing(), i, &view);
    fc_drawing_init(&flat);
    fc_expand(&view, &flat, doc_->lib());
    sketch(p, flat);
    fc_drawing_free(&flat);
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
    QString t3 = QString::number(std::atan2(-(ruler_b_.y() - ruler_a_.y()),
                                            ruler_b_.x() - ruler_a_.x()) *
                                     180.0 / 3.14159265358979323846,
                                 'f', 2) +
                 "°";
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
    if (dirty_ || cache_.isNull()) renderCache();
    QPainter p(this);
    p.drawImage(QPointF(0, 0), cache_);
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
        for (int i : sel_) sketchIndex(p, i);
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
    if (cheats_) {
        QList<QPair<QString, QString>> rows = {
            {"send", ""},
            {"Return", "send the drawing to the agent — the window stays open"},
            {"Ctrl+Return", "done: save and close"},
            {"I", "show or hide the bottom info bar"},
            {"K", "this sheet — type to search, Esc closes"},
            {"tools", ""},
            {"S", "select and move"},
            {"L", "line"},
            {"B", "rectangle"},
            {"E", "ellipse"},
            {"J", "connection dot"},
            {"T", "text"},
            {"Ctrl+B", "symbol library: click a macro, then click the canvas"},
            {"Esc", "finish the operation / back to select / clear selection"},
            {"right-click", "finish a line, or cancel the active tool"},
            {"middle-drag", "pan the view"},
            {"edit", ""},
            {"drag handle", "reshape selection or move its name/value independently"},
            {"Ctrl+click handle", "toggle handles, then drag one to move them together"},
            {"box-select handles", "enclose name/value handles without enclosing a whole element"},
            {"P", "properties of the selected element"},
            {"F2", "properties of the selected element"},
            {"double-click", "properties of the element under the cursor"},
            {"R", "rotate the selection 90° clockwise"},
            {"M", "mirror the selection horizontally"},
            {"arrow keys", "nudge the selection by one grid step"},
            {"Del", "delete the selection"},
            {"Backspace", "delete the selection"},
            {"Ctrl+C", "copy"},
            {"Ctrl+X", "cut"},
            {"Ctrl+V", "paste — follows the cursor, click to place"},
            {"Ctrl+D", "duplicate"},
            {"Ctrl+A", "select all"},
            {"Ctrl+Z", "undo"},
            {"Ctrl+Shift+Z", "redo"},
            {"Ctrl+Y", "redo"},
            {"[", "previous layer (also moves the selection)"},
            {"]", "next layer (also moves the selection)"},
            {"F", "filled rectangles and ellipses"},
            {"G", "grid dots on/off"},
            {"Shift+G", "snap to grid on/off"},
            {"view", ""},
            {"wheel", "zoom"},
            {"two-finger drag", "pan the view (touchpad)"},
            {"pinch", "zoom at the fingers (touchpad)"},
            {"+ / =", "zoom in"},
            {"-", "zoom out"},
            {"0 / Space", "fit the drawing (and tracing picture) in the window"},
            {"H", "tracing picture on/off (bg_image setting)"},
            {"right-drag", "ruler: length, mm and angle"},
            {"Shift+drag", "ruler — works from any tool"},
            {"Ctrl+S", "save"},
            {"Ctrl+W", "close the window"},
        };
        QList<QPair<QString, QString>> shown;
        QString q = cheats_query_.trimmed().toLower();
        if (q.isEmpty()) {
            shown = rows;
        } else {
            QPair<QString, QString> head;
            QList<QPair<QString, QString>> items;
            auto hit = [&q](const QPair<QString, QString> &r) {
                return r.first.toLower().contains(q) || r.second.toLower().contains(q);
            };
            auto flush = [&] {
                bool headMatch = !head.first.isEmpty() && head.first.toLower().contains(q);
                bool any = false;
                for (const auto &it : items)
                    if (headMatch || hit(it)) any = true;
                if (!any) return;
                if (!head.first.isEmpty()) shown << head;
                for (const auto &it : items)
                    if (headMatch || hit(it)) shown << it;
            };
            for (const auto &r : rows) {
                if (r.second.isEmpty()) {
                    flush();
                    head = r;
                    items.clear();
                } else {
                    items << r;
                }
            }
            flush();
        }
        paintCard(p, shown, &cheats_query_, &cheats_scroll_);
    }
    if (!cheats_) cheats_bar_->hide();
}

void Canvas::resizeEvent(QResizeEvent *) {
    if (auto_fit_) fit();
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
    uint32_t rgb = doc_->drawing().layers[l].rgb;
    /* same adaptation as paperView: near-black ink needs the theme foreground
     * to stay readable on dark paper */
    if (luma(toRgb(paper())) < 0.5 && luma(rgb) < 0.2) return theme_->fg;
    return QColor::fromRgb(rgb);
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
                 .arg(QString::number(std::atan2(-(ruler_b_.y() - ruler_a_.y()),
                                                 ruler_b_.x() - ruler_a_.x()) *
                                      180.0 / 3.14159265358979323846,
                                     'f', 2));
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
    update();
}

void Canvas::finishLine() {
    have_start_ = false;
    update();
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

void Canvas::mousePressEvent(QMouseEvent *e) {
    if (form_) form_->apply(); /* clicking away keeps what was typed */
    if (cheats_) {
        if (!cheats_box_.contains(e->position().toPoint())) toggleCheatsheet();
        setFocus();
        update();
        return;
    }
    /* a new press clears a measurement left on screen from a previous drag */
    if (ruler_active_) ruler_active_ = false;
    setFocus();
    press_px_ = e->position();
    press_lg_ = toLogical(e->position());
    QPointF s = snap(press_lg_);
    cursor_lg_ = s;

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

    switch (tool_) {
    case Select: {
        int hh = -1;
        int hp = handleAt(press_px_, &hh);
        if (hp >= 0) {
            auto handle = qMakePair(hp, hh);
            if (e->modifiers() & Qt::ControlModifier) {
                if (selected_handles_.contains(handle)) selected_handles_.removeAll(handle);
                else selected_handles_ << handle;
                status();
                update();
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
        int h = doc_->hit(press_lg_.x(), press_lg_.y(), 5.0 / scale_);
        bool add = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        if (h >= 0) {
            selected_handles_.clear();
            if (add) {
                QList<int> s2 = sel_;
                if (s2.contains(h)) s2.removeAll(h);
                else s2 << h;
                setSelection(s2);
            } else if (!sel_.contains(h)) {
                setSelection({h});
            }
            if (sel_.contains(h)) {
                op_ = Moving;
                moved_ = QPointF(0, 0);
                doc_->beginChange();
            }
        } else {
            if (!add) setSelection({});
            op_ = Banding;
        }
        break;
    }
    case Line:
        if (have_start_) {
            QPointF b = s;
            if (e->modifiers() & Qt::ShiftModifier) {
                if (std::abs(b.x() - start_.x()) > std::abs(b.y() - start_.y())) b.setY(start_.y());
                else b.setX(start_.x());
            }
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
            if (s != start_) {
                QString code = tool_ == Rect ? (settings_->getBool("filled") ? "RP" : "RV")
                                             : (settings_->getBool("filled") ? "EP" : "EV");
                doc_->append(QString("%1 %2 %3 %4 %5 %6")
                                 .arg(code, num(start_.x()), num(start_.y()), num(s.x()), num(s.y()))
                                 .arg(layer()));
            }
            have_start_ = false;
        } else {
            start_ = s;
            have_start_ = true;
            op_ = Drawing;
        }
        break;
    case Dot:
    case Text:
    case Macro:
        place(s);
        break;
    }
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *e) {
    if (cheats_) return;
    QPointF lg = toLogical(e->position());
    has_cursor_ = true;
    QPointF s = snap(lg);
    if (tool_ == Line && have_start_ && (e->modifiers() & Qt::ShiftModifier)) {
        if (std::abs(s.x() - start_.x()) > std::abs(s.y() - start_.y())) s.setY(start_.y());
        else s.setX(start_.x());
    }
    cursor_lg_ = op_ == Banding ? lg : s;

    if (op_ == RulerArmed) {
        if ((e->position() - press_px_).manhattanLength() > 3) ruler_dragged_ = true;
        if (ruler_dragged_) {
            ruler_b_ = lg; /* unsnapped, like FidoCadJS */
            ruler_active_ = true;
            update();
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
        update();
    } else if (op_ == Panning) {
        QPointF d = e->position() - press_px_;
        if (!d.isNull()) auto_fit_ = false;
        ox_ += d.x();
        oy_ += d.y();
        press_px_ = e->position();
        dirty_ = true;
    } else if (op_ == Moving) {
        QPointF want = snap(lg) - snap(press_lg_);
        QPointF step = want - moved_;
        if (step != QPointF(0, 0)) {
            doc_->translateLive(sel_, step.x(), step.y());
            moved_ = want;
        }
    }
    status();
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (cheats_) return;
    QPointF lg = toLogical(e->position());
    if (op_ == RulerArmed) {
        op_ = None;
        if (ruler_dragged_) {
            /* leave the measurement on screen until the next press */
            update();
            return;
        }
        ruler_active_ = false;
        if (e->button() == Qt::RightButton) {
            if (tool_ == Line && have_start_) finishLine();
            else if (tool_ != Select) setTool(Select);
            return;
        }
        if (e->button() == Qt::LeftButton) {
            /* plain Shift-click: do what the press would have done */
            QPointF s = snap(lg);
            bool add = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            switch (tool_) {
            case Select: {
                int h = doc_->hit(lg.x(), lg.y(), 5.0 / scale_);
                if (h >= 0) {
                    if (add) {
                        QList<int> s2 = sel_;
                        if (s2.contains(h)) s2.removeAll(h);
                        else s2 << h;
                        setSelection(s2);
                    } else if (!sel_.contains(h)) {
                        setSelection({h});
                    }
                } else if (!add) {
                    setSelection({});
                }
                break;
            }
            case Line:
                start_ = s;
                have_start_ = true;
                break;
            case Rect:
            case Oval:
                start_ = s;
                have_start_ = true;
                break;
            case Dot:
            case Text:
            case Macro:
                place(s);
                break;
            }
            update();
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
        update();
        return;
    }
    if (op_ == Drawing) {
        op_ = None;
        /* dragged a rect/oval: finish on release; a plain click waits for the 2nd corner */
        QPointF s = snap(lg);
        if ((e->position() - press_px_).manhattanLength() > 6 && s != start_) {
            QString code = tool_ == Rect ? (settings_->getBool("filled") ? "RP" : "RV")
                                         : (settings_->getBool("filled") ? "EP" : "EV");
            doc_->append(QString("%1 %2 %3 %4 %5 %6")
                             .arg(code, num(start_.x()), num(start_.y()), num(s.x()), num(s.y()))
                             .arg(layer()));
            have_start_ = false;
        }
    }
    update();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    if (cheats_) return;
    if (tool_ == Line) {
        finishLine();
        return;
    }
    if (tool_ != Select) return;
    QPointF lg = toLogical(e->position());
    int h = doc_->hit(lg.x(), lg.y(), 5.0 / scale_);
    if (h < 0) return;
    setSelection({h});
    editSelection();
}

void Canvas::wheelEvent(QWheelEvent *e) {
    if (cheats_) {
        int d = e->pixelDelta().isNull() ? e->angleDelta().y() / 2 : e->pixelDelta().y();
        cheats_scroll_ = qBound(0, cheats_scroll_ - d, cheats_bar_->maximum());
        update();
        return;
    }
    QPoint pd = e->pixelDelta();
    if (!pd.isNull() && !(e->modifiers() & Qt::ControlModifier)) {
        /* touchpad two-finger scroll pans */
        panBy(pd);
        return;
    }
    /* Ctrl+scroll zooms; some stacks deliver pinch that way too */
    double steps = e->angleDelta().y() / 120.0;
    if (steps != 0) zoomBy(std::pow(1.2, steps), e->position());
}

bool Canvas::event(QEvent *e) {
    if (e->type() != QEvent::NativeGesture) return QWidget::event(e);
    /* Wayland (zwp_pointer_gestures), X11 XInput 2.4 and macOS deliver
       touchpad pinch as native gestures; value() is the incremental scale */
    auto *g = static_cast<QNativeGestureEvent *>(e);
    if (cheats_) return true;
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
    update();
}

/* ---------- keyboard-level actions (bound as QActions by the window) ---------- */

void Canvas::keyPressEvent(QKeyEvent *e) {
    if (cheats_) {
        if (e->key() == Qt::Key_K || e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return ||
            e->key() == Qt::Key_Enter) {
            if (e->key() == Qt::Key_K) {
                toggleCheatsheet();
            } else {
                cheats_ = false;
                update();
            }
            return;
        }
        if (e->key() == Qt::Key_Backspace) {
            cheats_query_.chop(1);
            cheats_scroll_ = 0;
            update();
            return;
        }
        switch (e->key()) {
        case Qt::Key_Down: cheats_scroll_ += 24; update(); return;
        case Qt::Key_Up: cheats_scroll_ -= 24; update(); return;
        case Qt::Key_PageDown: cheats_scroll_ += qMax(24, height() * 2 / 3); update(); return;
        case Qt::Key_PageUp: cheats_scroll_ -= qMax(24, height() * 2 / 3); update(); return;
        case Qt::Key_Home: cheats_scroll_ = 0; update(); return;
        case Qt::Key_End: cheats_scroll_ = 1 << 30; update(); return;
        case Qt::Key_Left:
        case Qt::Key_Right: return; /* no nudging while the sheet is open */
        default: break;
        }
        if (!e->text().isEmpty() && e->text().at(0).isPrint()) {
            cheats_query_ += e->text();
            cheats_scroll_ = 0;
            update();
            return;
        }
        return;
    }
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (!mods) {
        bool shift = e->modifiers() & Qt::ShiftModifier;
        switch (e->key()) {
        case Qt::Key_K: toggleCheatsheet(); return;
        case Qt::Key_I:
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
        case Qt::Key_H: flipSetting("show_bg_image"); return;
        case Qt::Key_0: fit(); return;
        case Qt::Key_Space: fit(); return;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomBy(1.25, rect().center()); return;
        case Qt::Key_Minus: zoomBy(0.8, rect().center()); return;
        default: break;
        }
    }
    if (e->key() == Qt::Key_Escape) {
        if (ruler_active_) { ruler_active_ = false; update(); return; }
        if (tool_ == Line && have_start_) finishLine();
        else if (tool_ != Select) setTool(Select);
        else setSelection({});
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !mods) {
        if (tool_ == Line && have_start_) finishLine();
        else if (onSend) onSend();
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

void Canvas::keyReleaseEvent(QKeyEvent *e) { QWidget::keyReleaseEvent(e); }

void Canvas::rotateSelection() {
    if (tool_ == Macro) {
        macro_orient_ = (macro_orient_ + 1) & 3;
        update();
        return;
    }
    doc_->rotate(sel_);
}

void Canvas::mirrorSelection() {
    if (tool_ == Macro) {
        macro_mirror_ = !macro_mirror_;
        update();
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
    QString text = QApplication::clipboard()->text();
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
    update();
}

void Canvas::flipSetting(const QString &key) {
    settings_->set(key, settings_->getBool(key) ? "0" : "1");
    if (onSettingChanged) onSettingChanged(key);
    refresh();
}

void Canvas::toggleCheatsheet() {
    cheats_ = !cheats_;
    if (cheats_) {
        cheats_query_.clear();
        cheats_scroll_ = 0;
    }
    if (!cheats_) cheats_bar_->hide();
    update();
}

void Canvas::paintCard(QPainter &p, const QList<QPair<QString, QString>> &rows,
                       const QString *search, int *scroll) {
    p.save();
    QFont sheetFont = font();
    sheetFont.setPointSizeF(qMax(14.0, sheetFont.pointSizeF() + 3.0));
    p.setFont(sheetFont);
    QFontMetrics fm(sheetFont);
    const int pad = 20, gap = 24, rowPad = 12;
    const int w = qMin(width() - 24, 1000);
    const int h = qMin(height() - 24, 640);
    const QRect box((width() - w) / 2, (height() - h) / 2, w, h);
    cheats_box_ = box;
    p.setPen(QPen(theme_->accent, 2));
    p.setBrush(theme_->bg);
    p.drawRect(box);

    /* Fixed search header and footer, like Omarchy's keybinding launcher. */
    const int headerH = fm.height() + 2 * rowPad;
    const QRect header = box.adjusted(pad, 0, -pad, 0);
    p.setPen(theme_->accent);
    p.drawText(QRect(header.left(), header.top(), fm.horizontalAdvance(">"), headerH),
               Qt::AlignVCenter, ">");
    p.setPen(search && !search->isEmpty() ? theme_->fg : theme_->fg_dim);
    const QString query = search && !search->isEmpty() ? *search + "|" : "Search keybindings…";
    p.drawText(QRect(header.left() + gap, header.top(), header.width() - gap, headerH),
               Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(query, Qt::ElideLeft, header.width() - gap));
    p.setPen(theme_->muted);
    p.drawLine(box.left(), box.top() + headerH, box.right(), box.top() + headerH);

    const int footerH = fm.height() + rowPad;
    const int viewTop = box.top() + headerH + 8;
    const int viewH = qMax(1, h - headerH - footerH - 16);
    const int contentW = w - 2 * pad - 12;
    int keyW = 0;
    for (const auto &r : rows)
        if (!r.second.isEmpty()) keyW = qMax(keyW, fm.horizontalAdvance(r.first));
    const bool stacked = contentW < keyW + gap + fm.horizontalAdvance("properties of element");
    const int valueW = stacked ? contentW : contentW - keyW - gap;
    QList<int> heights;
    int contentH = 0;
    for (const auto &r : rows) {
        int rowH;
        if (r.second.isEmpty()) {
            rowH = fm.height() + rowPad;
        } else {
            const int textH = fm.boundingRect(QRect(0, 0, valueW, 10000),
                                               Qt::TextWordWrap, r.second).height();
            rowH = qMax(fm.height(), textH) + rowPad + (stacked ? fm.height() + 4 : 0);
        }
        heights << rowH;
        contentH += rowH;
    }
    const int maxScroll = qMax(0, contentH - viewH);
    const int sc = scroll ? qBound(0, *scroll, maxScroll) : 0;
    if (scroll) *scroll = sc;
    {
        const QSignalBlocker blocked(cheats_bar_);
        cheats_bar_->setRange(0, maxScroll);
        cheats_bar_->setPageStep(viewH);
        cheats_bar_->setSingleStep(fm.height() + rowPad);
        cheats_bar_->setValue(sc);
        cheats_bar_->setGeometry(box.right() - 13, viewTop, 8, viewH);
        cheats_bar_->setVisible(maxScroll > 0);
    }

    p.save();
    p.setClipRect(QRect(box.left() + pad, viewTop, contentW, viewH));
    int cy = viewTop - sc;
    for (int i = 0; i < rows.size(); ++i) {
        const auto &r = rows[i];
        if (r.second.isEmpty()) {
            p.setPen(theme_->fg_dim);
            p.drawText(QRect(box.left() + pad, cy, contentW, heights[i]),
                       Qt::AlignVCenter | Qt::AlignLeft, r.first.toUpper());
        } else {
            p.setPen(theme_->accent);
            p.drawText(QRect(box.left() + pad, cy + rowPad / 2, stacked ? contentW : keyW, fm.height()),
                       Qt::AlignLeft | Qt::AlignTop, r.first);
            p.setPen(theme_->fg);
            const int vx = box.left() + pad + (stacked ? 0 : keyW + gap);
            const int vy = cy + rowPad / 2 + (stacked ? fm.height() + 4 : 0);
            p.drawText(QRect(vx, vy, valueW, heights[i] - (vy - cy)),
                       Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, r.second);
        }
        cy += heights[i];
    }
    if (rows.isEmpty()) {
        p.setPen(theme_->fg_dim);
        p.drawText(QRect(box.left() + pad, viewTop, contentW, viewH),
                   Qt::AlignCenter, "No matching keybindings");
    }
    p.restore();

    const int footerY = box.bottom() - footerH;
    p.setPen(theme_->muted);
    p.drawLine(box.left(), footerY, box.right(), footerY);
    p.setPen(theme_->fg_dim);
    const QString hint = w < 620 ? "↑ ↓  Scroll   ·   Esc  Close"
                                 : "↑ ↓ / PgUp PgDn  Scroll   ·   Esc  Close";
    p.drawText(QRect(box.left() + pad, footerY, w - 2 * pad, footerH),
               Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(hint, Qt::ElideRight, w - 2 * pad));
    p.restore();
}
