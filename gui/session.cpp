#include "session.h"

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QTimer>

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
    /* A new GUI run is a new conversation: fresh logs, fresh read offsets. */
    for (const char *n : {"events.jsonl", "commands.jsonl", ".events.pos"}) {
        QFile::remove(file(n));
    }
    {
        QFile events(file("events.jsonl"));
        (void)events.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QFile cmds(file("commands.jsonl"));
        (void)cmds.open(QIODevice::WriteOnly | QIODevice::Truncate);
    }
    pos_ = 0;
    watch_->addPath(file("commands.jsonl"));
    watch_->addPath(dir_);
    timer_->start();
    return true;
}

void Session::send(const QJsonObject &event) {
    QFile f(file("events.jsonl"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    f.write(QJsonDocument(event).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.flush();
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
