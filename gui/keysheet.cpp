#include "keysheet.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>

namespace {

using Row = QPair<QString, QString>; /* a heading has an empty description */

const QList<Row> kRows = {
        {"session", ""},
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
        {"0 / Space", "fit the drawing in the window"},
        {"right-drag", "ruler: length, mm and angle"},
        {"Shift+drag", "ruler — works from any tool"},
        {"Ctrl+S", "save"},
        {"Ctrl+W", "close the window"},
};

/* Rows matching the query; a heading matches all its rows, and a heading is
 * shown when any of its rows is. */
QList<Row> filtered(const QString &query) {
    QString q = query.trimmed().toLower();
    if (q.isEmpty()) return kRows;
    QList<Row> shown, items;
    Row head;
    auto hit = [&q](const Row &r) {
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
    for (const auto &r : kRows) {
        if (r.second.isEmpty()) {
            flush();
            head = r;
            items.clear();
        } else {
            items << r;
        }
    }
    flush();
    return shown;
}

} // namespace

KeySheet::KeySheet(const Theme *theme, QWidget *canvas) : QWidget(canvas), theme_(theme) {
    setObjectName("keySheet");
    setAttribute(Qt::WA_NoMousePropagation);
    setFocusPolicy(Qt::StrongFocus);
    bar_ = new QScrollBar(Qt::Vertical, this);
    bar_->setObjectName("cheatsheetScrollBar");
    bar_->setFocusPolicy(Qt::NoFocus);
    bar_->hide();
    connect(bar_, &QScrollBar::valueChanged, this, [this](int value) {
        scroll_ = value;
        update();
    });
    hide();
}

void KeySheet::open() {
    query_.clear();
    scroll_ = 0;
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
}

void KeySheet::dismiss() {
    hide();
    parentWidget()->setFocus();
}

void KeySheet::keyPressEvent(QKeyEvent *e) {
    const int page = qMax(24, height() * 2 / 3);
    switch (e->key()) {
    case Qt::Key_K:
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter: dismiss(); return;
    case Qt::Key_Backspace:
        query_.chop(1);
        scroll_ = 0;
        break;
    case Qt::Key_Down: scroll_ += 24; break;
    case Qt::Key_Up: scroll_ -= 24; break;
    case Qt::Key_PageDown: scroll_ += page; break;
    case Qt::Key_PageUp: scroll_ -= page; break;
    case Qt::Key_Home: scroll_ = 0; break;
    case Qt::Key_End: scroll_ = 1 << 30; break;
    case Qt::Key_Left:
    case Qt::Key_Right: return;
    default:
        if (e->text().isEmpty() || !e->text().at(0).isPrint()) return;
        query_ += e->text();
        scroll_ = 0;
    }
    update();
}

void KeySheet::mousePressEvent(QMouseEvent *e) {
    if (!box_.contains(e->position().toPoint())) dismiss();
}

void KeySheet::wheelEvent(QWheelEvent *e) {
    int d = e->pixelDelta().isNull() ? e->angleDelta().y() / 2 : e->pixelDelta().y();
    scroll_ = qBound(0, scroll_ - d, bar_->maximum());
    update();
}

bool KeySheet::event(QEvent *e) {
    if (e->type() == QEvent::NativeGesture) { /* no zooming or panning under the sheet */
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void KeySheet::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QList<Row> rows = filtered(query_);
    QFont sheetFont = font();
    sheetFont.setPointSizeF(qMax(14.0, sheetFont.pointSizeF() + 3.0));
    p.setFont(sheetFont);
    QFontMetrics fm(sheetFont);
    const int pad = 20, gap = 24, rowPad = 12;
    const int w = qMin(width() - 24, 1000);
    const int h = qMin(height() - 24, 640);
    const QRect box((width() - w) / 2, (height() - h) / 2, w, h);
    box_ = box;
    p.setPen(QPen(theme_->accent, 2));
    p.setBrush(theme_->bg);
    p.drawRect(box);

    /* Fixed search header and footer, like Omarchy's keybinding launcher. */
    const int headerH = fm.height() + 2 * rowPad;
    const QRect header = box.adjusted(pad, 0, -pad, 0);
    p.setPen(theme_->accent);
    p.drawText(QRect(header.left(), header.top(), fm.horizontalAdvance(">"), headerH),
               Qt::AlignVCenter, ">");
    p.setPen(query_.isEmpty() ? theme_->fg_dim : theme_->fg);
    const QString query = query_.isEmpty() ? QString("Search keybindings…") : query_ + "|";
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
    const int sc = qBound(0, scroll_, maxScroll);
    scroll_ = sc;
    {
        const QSignalBlocker blocked(bar_);
        bar_->setRange(0, maxScroll);
        bar_->setPageStep(viewH);
        bar_->setSingleStep(fm.height() + rowPad);
        bar_->setValue(sc);
        bar_->setGeometry(box.right() - 13, viewTop, 8, viewH);
        bar_->setVisible(maxScroll > 0);
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
}
