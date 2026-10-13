#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>

class QFileSystemWatcher;
class QTimer;

/* File-based channel between this GUI and the agent that launched it.
 *
 *   DIR/events.jsonl    GUI -> agent. Append-only JSON lines; the agent reads
 *                       them with `fcai session wait DIR`, which remembers its
 *                       own offset in DIR/.events.pos.
 *   DIR/commands.jsonl  agent -> GUI, via `fcai session push DIR ...`,
 *                       `... push`, `... set`, `... get`, `... quit`.
 *
 * The GUI archives the previous logs at start, then polls commands.jsonl and
 * calls onCommand for every complete line. Nothing here needs Q_OBJECT. */
class Session {
public:
    Session(const QString &dir, QObject *owner);
    bool start();
    void send(const QJsonObject &event);
    void pollNow();
    QString error() const { return err_; }

    std::function<void(const QJsonObject &)> onCommand;

private:
    QString file(const char *name) const;
    QString dir_;
    QString err_;
    QFileSystemWatcher *watch_ = nullptr;
    QTimer *timer_ = nullptr;
    qint64 pos_ = 0;
    qint64 seq_ = 0;
    QString run_;
};
