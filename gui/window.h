#pragma once

#include "canvas.h"
#include "document.h"
#include "session.h"
#include "settings.h"
#include "theme.h"

#include <QMainWindow>
#include <functional>

class QAction;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QTimer;
class QTreeWidget;

/* The window is the drawing plus an optional bottom bar (hidden by default,
 * toggled with I): canvas status on the left, a keybindings hint on the
 * right. There is no chat panel — the agent that launched the GUI is the
 * assistant, and the Session is the channel to it. */
class MainWindow : public QMainWindow {
public:
    MainWindow(Document *doc, Settings *settings, Theme *theme, QApplication *app,
               const QString &sessionDir);
    ~MainWindow() override;

    Canvas *canvas() const { return canvas_; }
    Session *session() const { return session_; }
    bool barVisible() const;
    void setAgentTitle(const QString &t);
    /* Size from window_size; with float=1 on Hyprland (Omarchy) register a rule
     * so the window maps floating and centred. Call before show(). */
    void placeWindow();
    void bringToFront();

    /* How the session ended: "done" (Done/Ctrl+Return) or "closed". */
    QString status() const { return status_; }
    QString note() const { return note_; }
    bool saved() const { return saved_ok_; }
    QString saveError() const { return save_err_; }

    std::function<void()> onFinished;

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void buildShortcuts();
    void buildBar();
    QWidget *buildPalette();
    void fillPalette(const QString &filter);
    void updatePreview(); /* library panel: render the selected macro */
    void applyTheme();
    void settingChanged(const QString &key);
    void docChanged();
    void updateTitle();
    void updateInfo(const QString &s);
    bool save();
    void done(const QString &note);
    /* Live feedback: user edits and selection changes reach the agent as a
     * debounced `change` event; agent commands and disk reloads do not. */
    void scheduleChange(bool drawing);
    void flushChange();
    void toggleBar();                      /* I */
    void report(const QString &text);      /* transient message in the bottom bar */
    void applyCommand(const QJsonObject &cmd);
    void sendEvent(const QString &event, QJsonObject extra = QJsonObject());
    void watchFile();

    Document *doc_;
    Settings *settings_;
    Theme *theme_;
    QApplication *app_;
    Canvas *canvas_;
    Session *session_;
    QWidget *palette_;
    QLineEdit *filter_;
    QTreeWidget *tree_;
    QLabel *preview_ = nullptr;
    QString preview_key_;
    QLabel *info_;
    QFileSystemWatcher *watch_;
    QString title_;
    QString status_ = "closed", note_, save_err_;
    bool saved_ok_ = false, finishing_ = false;
    bool disk_changed_ = false;
    QByteArray previous_event_;
    QList<int> previous_selection_;
    QTimer *live_ = nullptr;
    bool agent_ = false; /* applying an agent command or a disk reload */
    bool live_drawing_ = false, live_selection_ = false;
    QByteArray conflict_disk_;
};
