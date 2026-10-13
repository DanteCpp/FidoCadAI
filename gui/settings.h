#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

/* Every GUI knob lives here. They are stored as `key = value` in
 * $XDG_CONFIG_HOME/fidocad/gui.conf and changed by the calling agent through
 * the session (`fcai session set DIR KEY VALUE`) or by editing the file.
 * Add new knobs to kKnobs in settings.cpp; there are no settings dialogs. */
class Settings {
public:
    explicit Settings(const QString &path = QString());

    QString path() const { return path_; }
    bool load();
    bool save() const;

    QString get(const QString &key) const;
    int getInt(const QString &key) const;
    bool getBool(const QString &key) const;
    /* Returns an error message, empty on success. */
    QString set(const QString &key, const QString &value);
    QStringList keys() const;

private:
    QString path_;
    QMap<QString, QString> values_;
};
