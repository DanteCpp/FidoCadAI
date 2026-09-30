#include "session.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QTimer>
#include <QSaveFile>
#include <QUuid>

Session::Session(const QString &dir, QObject *owner) : dir_(dir) {
    watch_ = new QFileSystemWatcher(owner);
    timer_ = new QTimer(owner);
    timer_->setInterval(250);
    QObject::connect(timer_, &QTimer::timeout, owner, [this] { pollNow(); });
    QObject::connect(watch_, &QFileSystemWatcher::fileChanged, owner, [this] { pollNow(); });
    QObject::connect(watch_, &QFileSystemWatcher::directoryChanged, owner, [this] { pollNow(); });
}

QString Session::file(const char *name) const { return dir_ + "/" + name; }

bool Session::start() {
    if (dir_.isEmpty()) {
        err_ = "no session directory";
        return false;
    }
    if (!QDir().mkpath(dir_)) {
        err_ = "cannot create " + dir_;
        return false;
    }
    /* Keep old conversations, but never replay their commands into a new GUI. */
    run_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString archive = file("history") + "/" +
        QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz") + "-" + run_;
    for (const char *n : {"events.jsonl", "commands.jsonl", ".events.pos"}) {
        if (QFile::exists(file(n)) &&
            (!QDir().mkpath(archive) || !QFile::rename(file(n), archive + "/" + n))) {
            err_ = "cannot archive " + file(n);
            return false;
        }
    }
    {
        QFile events(file("events.jsonl"));
        QFile cmds(file("commands.jsonl"));
        if (!events.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            !cmds.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            !QDir().mkpath(file("replies"))) {
            err_ = "cannot create session files";
            return false;
        }
    }
    pos_ = 0;
    watch_->addPath(file("commands.jsonl"));
    watch_->addPath(dir_);
    timer_->start();
    return true;
}

void Session::send(const QJsonObject &event) {
    QJsonObject e = event;
    e["seq"] = ++seq_;
    e["session_id"] = run_;
    e["time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QByteArray line = QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n';
    QFile f(file("events.jsonl"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    f.write(line);
    f.flush();
    /* Correlated replies do not consume the agent's pending user events. */
    QString id = e["id"].toString();
    if (id.isEmpty() || id.size() > 100) return;
    for (QChar c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return;
    QSaveFile reply(file("replies") + "/" + id + ".json");
    if (reply.open(QIODevice::WriteOnly)) {
        if (reply.write(line) == line.size()) reply.commit();
    }
}

void Session::pollNow() {
    QFile f(file("commands.jsonl"));
    if (!f.open(QIODevice::ReadOnly)) return;
    if (f.size() <= pos_) {
        pos_ = f.size();
        return;
    }
    if (!f.seek(pos_)) return;
    while (!f.atEnd()) {
        qint64 start = f.pos();
        QByteArray line = f.readLine();
        if (!line.endsWith('\n')) {
            f.seek(start); /* partial write: try again on the next poll */
            break;
        }
        pos_ = f.pos();
        line = line.trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        if (onCommand) onCommand(doc.object());
    }
    /* the watcher can drop a path if the file is replaced; keep it registered */
    if (!watch_->files().contains(file("commands.jsonl"))) watch_->addPath(file("commands.jsonl"));
}
