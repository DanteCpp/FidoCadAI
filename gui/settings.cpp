#include "settings.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

struct Knob {
    const char *key, *def, *help;
};

/* Order matters: this is what get_settings/fcai session get print. */
const Knob kKnobs[] = {
    {"grid", "5", "snap/grid step in FidoCad units"},
    {"snap", "1", "snap clicks to the grid (0/1)"},
    {"show_grid", "1", "draw grid dots (0/1)"},
    {"layer", "0", "layer for new primitives (0-15)"},
    {"filled", "0", "new rectangles/ellipses are filled (0/1)"},
    {"theme", "omarchy", "omarchy | light | dark"},
    {"paper", "theme", "canvas colour: theme | white | #RRGGBB"},
    {"font", "", "UI font family (empty = Omarchy/monospace)"},
    {"font_size", "11", "UI font size in points"},
    {"tex", "1", "typeset $...$ LaTeX in text (0/1)"},
    {"palette", "0", "show the symbol library panel (0/1, Ctrl+B)"},
    {"statusbar", "0", "show the bottom info/message bar (0/1, I)"},
    {"float", "0", "Hyprland/Omarchy: open floating, centred (0/1; next launch)"},
    {"window_size", "85%", "initial window size: N% of the screen or WxH"},
    {"autosave", "0", "write the .fcd after every edit (0/1)"},
};

bool isBool(const QString &k) {
    return k == "snap" || k == "show_grid" || k == "filled" || k == "tex" || k == "palette" ||
           k == "statusbar" || k == "float" || k == "autosave";
}

QString normBool(const QString &v, bool *ok) {
    QString s = v.trimmed().toLower();
    *ok = true;
    if (s == "1" || s == "true" || s == "on" || s == "yes") return "1";
    if (s == "0" || s == "false" || s == "off" || s == "no") return "0";
    *ok = false;
    return v;
}

QString defaultPath() {
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) base = QDir::homePath() + "/.config";
    return base + "/fidocad/gui.conf";
}

} // namespace

Settings::Settings(const QString &path) : path_(path.isEmpty() ? defaultPath() : path) {
    for (const Knob &k : kKnobs) values_[k.key] = k.def;
}

bool Settings::load() {
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        QString k = line.left(eq).trimmed(), v = line.mid(eq + 1).trimmed();
        if (values_.contains(k)) values_[k] = v;
    }
    return true;
}

bool Settings::save() const {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QFile f(path_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream out(&f);
    out << "# fcai-gui settings — change them with `fcai session set DIR key value`\n";
    /* only what differs from the defaults, so improved defaults reach old configs */
    for (const Knob &k : kKnobs)
        if (values_.value(k.key) != k.def) out << k.key << " = " << values_.value(k.key) << "\n";
    f.close();
    return true;
}

QString Settings::get(const QString &key) const { return values_.value(key); }
int Settings::getInt(const QString &key) const { return values_.value(key).toInt(); }
bool Settings::getBool(const QString &key) const { return values_.value(key) == "1"; }

QString Settings::set(const QString &key, const QString &value) {
    QString k = key.trimmed().toLower(), v = value.trimmed();
    if (!values_.contains(k))
        return QString("unknown setting '%1'. Known: %2").arg(k, keys().join(", "));
    if (isBool(k)) {
        bool ok;
        v = normBool(v, &ok);
        if (!ok) return QString("%1 must be 0 or 1").arg(k);
    } else if (k == "grid") {
        bool ok;
        int g = v.toInt(&ok);
        if (!ok || g < 1 || g > 100) return "grid must be an integer 1..100";
    } else if (k == "layer") {
        bool ok;
        int l = v.toInt(&ok);
        if (!ok || l < 0 || l > 15) return "layer must be 0..15";
    } else if (k == "font_size") {
        bool ok;
        int s = v.toInt(&ok);
        if (!ok || s < 6 || s > 40) return "font_size must be 6..40";
    } else if (k == "theme") {
        v = v.toLower();
        if (v != "omarchy" && v != "light" && v != "dark") return "theme must be omarchy, light or dark";
    } else if (k == "paper") {
        v = v.toLower();
        if (v != "theme" && v != "white" && !QColor::isValidColorName(v))
            return "paper must be theme, white or a colour like #ffffff";
    } else if (k == "window_size") {
        static const QRegularExpression re("^([1-9][0-9]?|100)%$|^[0-9]{3,5}x[0-9]{3,5}$");
        v = v.toLower();
        if (!re.match(v).hasMatch()) return "window_size must be N% (1-100) or WxH, e.g. 85% or 1400x860";
    }
    values_[k] = v;
    save();
    return QString();
}

QStringList Settings::keys() const {
    QStringList out;
    for (const Knob &k : kKnobs) out << k.key;
    return out;
}

QString Settings::describe(const QString &key) const {
    for (const Knob &k : kKnobs)
        if (key == k.key) return k.help;
    return QString();
}

QString Settings::dump(bool with_help) const {
    QString out;
    for (const Knob &k : kKnobs) {
        out += QString("%1 = %2").arg(k.key, values_.value(k.key));
        if (with_help) out += QString("    # %1").arg(k.help);
        out += "\n";
    }
    out += QString("(file: %1)\n").arg(path_);
    return out;
}
