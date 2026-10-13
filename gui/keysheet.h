#pragma once

#include "theme.h"

#include <QWidget>

class QScrollBar;

/* The keybinding sheet (K): a card over the canvas listing every shortcut,
 * filtered as you type and scrolled with the arrows, PgUp/PgDn, Home/End,
 * the wheel or its scrollbar. While open it has the focus and takes all
 * pointer input, so the canvas underneath never sees it. K, Esc, Return or a
 * click outside the card closes it. */
class KeySheet : public QWidget {
public:
    KeySheet(const Theme *theme, QWidget *canvas);

    void open();
    void dismiss();
    QString query() const { return query_; }
    int scroll() const { return scroll_; }

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    bool event(QEvent *) override;

private:
    const Theme *theme_;
    QString query_;
    int scroll_ = 0; /* clamped to the content when painted */
    QRect box_;      /* the card, in sheet coordinates */
    QScrollBar *bar_;
};
