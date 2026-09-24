#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTextStream>

namespace {

Theme builtin(bool dark) {
    Theme t;
    t.dark = dark;
    t.source = "built-in";
    if (dark) { /* Tokyo Night, Omarchy's default */
        t.bg = QColor("#1a1b26");
        t.bg_dark = QColor("#13141c");
        t.bg_light = QColor("#24283b");
        t.fg = QColor("#a9b1d6");
        t.fg_dim = QColor("#565f89");
        t.accent = QColor("#7aa2f7");
        t.selection = QColor("#292e42");
        t.muted = QColor("#414868");
        t.red = QColor("#f7768e");
        t.green = QColor("#9ece6a");
        t.yellow = QColor("#e0af68");
    } else {
        t.bg = QColor("#f5f5f7");
        t.bg_dark = QColor("#e8e8ec");
        t.bg_light = QColor("#ffffff");
        t.fg = QColor("#2e3440");
        t.fg_dim = QColor("#7b8394");
        t.accent = QColor("#2e6fd8");
        t.selection = QColor("#d7e3fa");
        t.muted = QColor("#c5c9d3");
        t.red = QColor("#c93c3c");
        t.green = QColor("#3f8f3f");
        t.yellow = QColor("#b07d10");
    }
    return t;
}

} // namespace

QString Theme::omarchyColorsPath() {
    const QString home = QDir::homePath();
    const QString candidates[] = {home + "/.local/state/omarchy/current/theme/colors.toml",
                                  home + "/.config/omarchy/current/theme/colors.toml"};
    for (const QString &p : candidates)
        if (QFile::exists(p)) return p;
    return QString();
}

QString Theme::omarchyFont() {
    QFile f(QDir::homePath() + "/.config/alacritty/alacritty.toml");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    static const QRegularExpression re("normal\\s*=\\s*\\{[^}]*family\\s*=\\s*\"([^\"]+)\"");
    auto m = re.match(QString::fromUtf8(f.readAll()));
    return m.hasMatch() ? m.captured(1) : QString();
}

Theme Theme::load(const QString &mode) {
    if (mode == "light") return builtin(false);
    if (mode == "dark") return builtin(true);
    QString path = omarchyColorsPath();
    if (path.isEmpty()) return builtin(true);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return builtin(true);
    QMap<QString, QString> kv;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        int eq = line.indexOf('=');
        if (eq < 0 || line.startsWith('#')) continue;
        QString v = line.mid(eq + 1).trimmed();
        if (v.startsWith('"')) v = v.mid(1, v.lastIndexOf('"') - 1);
        kv[line.left(eq).trimmed()] = v;
    }
    Theme t = builtin(kv.value("mode", "dark") != "light");
    auto pick = [&](QColor &c, const char *key) {
        QColor v(kv.value(key));
        if (v.isValid()) c = v;
    };
    pick(t.bg, "background");
    pick(t.bg_dark, "dark_background");
    pick(t.bg_light, "lighter_background");
    pick(t.fg, "foreground");
    pick(t.fg_dim, "dark_foreground");
    pick(t.accent, "accent");
    pick(t.selection, "selection");
    pick(t.muted, "muted");
    pick(t.red, "red");
    pick(t.green, "green");
    pick(t.yellow, "yellow");
    t.dark = t.bg.lightnessF() < 0.5;
    t.source = path;
    return t;
}

void Theme::apply(QApplication &app, const QString &font, int font_size) const {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Base, bg_dark);
    p.setColor(QPalette::AlternateBase, bg_light);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::PlaceholderText, fg_dim);
    p.setColor(QPalette::Button, bg_light);
    p.setColor(QPalette::ButtonText, fg);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, bg);
    p.setColor(QPalette::ToolTipBase, bg_light);
    p.setColor(QPalette::ToolTipText, fg);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Disabled, QPalette::Text, fg_dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, fg_dim);
    app.setPalette(p);

    QString family = font.isEmpty() ? omarchyFont() : font;
    QFont f = family.isEmpty() ? QFont("monospace") : QFont(family);
    f.setPointSize(font_size > 0 ? font_size : 11);
    app.setFont(f);

    /* Flat, borderless look like the Omarchy shell: square corners, accent on focus. */
    app.setStyleSheet(QString(R"(
        QMainWindow, QWidget#chrome { background: %1; }
        QToolBar { background: %1; border: none; border-bottom: 1px solid %4; padding: 4px; spacing: 2px; }
        QToolButton { color: %2; background: transparent; border: 1px solid transparent; padding: 2px 6px; }
        QToolButton:hover { border-color: %4; }
        QToolButton:checked { color: %1; background: %3; }
        QToolButton#done { color: %1; background: %6; font-weight: bold; padding: 3px 14px; }
        QToolButton#done:hover { background: %3; }
        QStatusBar { background: %1; color: %5; border-top: 1px solid %4; }
        QSplitter::handle { background: %4; }
        QLineEdit, QPlainTextEdit, QTextBrowser, QTreeWidget, QComboBox {
            background: %7; color: %2; border: 1px solid %4; selection-background-color: %3;
            selection-color: %1; padding: 3px; }
        QLineEdit:focus, QPlainTextEdit:focus { border-color: %3; }
        QComboBox QAbstractItemView { background: %7; color: %2; border: 1px solid %4; }
        QTreeWidget::item:selected { background: %3; color: %1; }
        QHeaderView::section { background: %1; color: %5; border: none; }
        QLabel#dim { color: %5; }
        QLabel#preview { background: %7; border: 1px solid %4; }
        QScrollBar:vertical { background: %1; width: 8px; }
        QScrollBar::handle:vertical { background: %4; min-height: 24px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
        QPushButton { background: %8; color: %2; border: 1px solid %4; padding: 4px 12px; }
        QPushButton:hover { border-color: %3; }
        QDialog { background: %1; }
    )")
                          .arg(bg.name(), fg.name(), accent.name(), muted.name(), fg_dim.name(),
                               green.name(), bg_dark.name(), bg_light.name()));
}
