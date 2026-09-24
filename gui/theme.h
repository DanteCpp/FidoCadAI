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
    static QString omarchyColorsPath(); /* empty if not on Omarchy */
    static QString omarchyFont();       /* terminal font family, or empty */
    void apply(QApplication &app, const QString &font, int font_size) const;
};
