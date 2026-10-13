#pragma once

#include <QColor>
#include <QString>

class QApplication;

/* Colours for the chrome. "omarchy" reads the current Omarchy theme's
 * colors.toml, so the editor follows `omarchy theme set`. */
struct Theme {
    QColor bg, bg_dark, bg_light, fg, fg_dim, accent, selection, muted, red, green, yellow;
    bool dark = true;
    QString source; /* file the colours came from, or "built-in" */

    static Theme load(const QString &mode);
    /* Paper is dark when its luma is below one half. On dark paper, near-black
     * layer ink (FidoCad's default circuit layer) is drawn in fg so it stays
     * visible; saturated colours keep their own value. */
    static bool isDark(const QColor &paper);
    QColor ink(uint32_t layer_rgb, const QColor &paper) const;
    static QString omarchyColorsPath(); /* empty if not on Omarchy */
    static QString omarchyFont();       /* terminal font family, or empty */
    void apply(QApplication &app, const QString &font, int font_size) const;
};
