#include "window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

QByteArray hyprctl(const QStringList &args, bool *ok = nullptr) {
    QProcess p;
    p.start("hyprctl", args);
    bool done = p.waitForFinished(1500);
    if (ok) *ok = done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return p.readAllStandardOutput();
}

double luma(uint32_t rgb) {
    return (0.299 * ((rgb >> 16) & 255) + 0.587 * ((rgb >> 8) & 255) + 0.114 * (rgb & 255)) / 255.0;
}

/* Render one library macro into an image for the palette preview. Reuses the
 * C renderer (fc_render_rgb) so it matches the canvas and `fcai render`. */
QImage macroPreview(const Document *doc, const QString &key, int w, int h, const QColor &paper,
                    const QColor &fg) {
    const FcMacro *m = fc_lib_find(doc->lib(), key.toUtf8().constData());
    if (!m || !m->body || !*m->body) return QImage();
    FcDrawing body, flat;
    fc_drawing_init(&body);
    fc_drawing_init(&flat);
    fc_parse(&body, m->body, strlen(m->body));
    fc_expand(&body, &flat, doc->lib());

    QImage img(w, h, QImage::Format_RGB888);
    img.fill(paper);
    FcRect b;
    if (fc_bounds(&flat, doc->lib(), &b) && b.max_x > b.min_x && b.max_y > b.min_y) {
        FcView v;
        memset(&v, 0, sizeof v);
        const double mrg = 10;
        v.scale = std::min((w - 2 * mrg) / (b.max_x - b.min_x), (h - 2 * mrg) / (b.max_y - b.min_y));
        v.scale = std::clamp(v.scale, 0.25, 40.0);
        v.ox = w / 2.0 - (b.min_x + b.max_x) / 2 * v.scale;
        v.oy = h / 2.0 - (b.min_y + b.max_y) / 2 * v.scale;
        v.bg = ((uint32_t)paper.rgb()) & 0xFFFFFFu;
        FcDrawing view = flat; /* shallow copy: adjust ink for dark paper */
        if (paper.lightnessF() < 0.5)
            for (int i = 0; i < FC_MAX_LAYERS; i++)
                if (luma(view.layers[i].rgb) < 0.2)
                    view.layers[i].rgb = ((uint32_t)fg.rgb()) & 0xFFFFFFu;
        fc_render_rgb(&view, doc->lib(), &v, img.bits(), w, h, (int)img.bytesPerLine());
    }
    fc_drawing_free(&body);
    fc_drawing_free(&flat);
    return img;
}

} // namespace

MainWindow::MainWindow(Document *doc, Settings *settings, Theme *theme, QApplication *app,
                       const QString &sessionDir)
    : doc_(doc), settings_(settings), theme_(theme), app_(app) {
    canvas_ = new Canvas(doc_, settings_, theme_, this);
    palette_ = buildPalette();

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(palette_);
    split->addWidget(canvas_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({230, 1000});
    split->setCollapsible(1, false);
    split->setHandleWidth(1);
    split->setChildrenCollapsible(false);
    setCentralWidget(split);
    palette_->setVisible(settings_->getBool("palette"));
    buildBar();

    previous_event_ = doc_->fcd();
    session_ = sessionDir.isEmpty() ? nullptr : new Session(sessionDir, this);
    if (session_) {
        if (!session_->start()) {
            fprintf(stderr, "fcai-gui: session: %s\n", qPrintable(session_->error()));
            delete session_;
            session_ = nullptr;
        } else session_->onCommand = [this](const QJsonObject &cmd) { applyCommand(cmd); };
    }

    buildShortcuts();

    doc_->setGrid(settings_->getInt("grid"));
    doc_->onChanged = [this] { docChanged(); };
    canvas_->onSend = [this] { sendToAgent(QString()); };
    canvas_->onToggleBar = [this] { toggleBar(); };
    canvas_->onStatus = [this](const QString &s) { updateInfo(s); };
    canvas_->onSettingChanged = [this](const QString &k) { settingChanged(k); };

    watch_ = new QFileSystemWatcher(this);
    auto diskChanged = [this](const QString &p) {
        if (p == Theme::omarchyColorsPath()) {
            /* theme switch replaces the file; re-add it after the swap settles */
            QTimer::singleShot(200, this, [this] {
                applyTheme();
                watchFile();
            });
            return;
        }
        if (p != doc_->path()) return;
        QTimer::singleShot(100, this, [this] {
            QFile f(doc_->path());
            if (!f.open(QIODevice::ReadOnly)) return;
            QByteArray disk = f.readAll();
            FcDrawing tmp;
            fc_drawing_init(&tmp);
            fc_parse(&tmp, disk.constData(), (size_t)disk.size());
            char *out = nullptr;
            FcEmitOpts o = {1, 1};
            fc_emit(&tmp, &out, &o);
            QByteArray canon(out ? out : "");
            free(out);
            fc_drawing_free(&tmp);
            /* Atomic file replacements drop the watch even if text is unchanged. */
            watchFile();
            if (canon == doc_->fcd() || canon == doc_->savedFcd()) {
                disk_changed_ = false;
                conflict_disk_.clear();
                return;
            }
            if (!doc_->modified()) {
                QString err;
                if (doc_->reloadFromDisk(&err)) {
                    disk_changed_ = false;
                    canvas_->fitIfOutside();
                    report("reloaded from disk");
                } else {
                    report(err);
                }
            } else {
                disk_changed_ = true;
                report("file changed on disk, but you have unsaved edits — kept yours");
                if (session_ && canon != conflict_disk_) {
                    conflict_disk_ = canon;
                    QJsonObject event = stateEvent("conflict", "disk changed with unsaved GUI edits");
                    event["disk_fcd"] = QString::fromUtf8(canon);
                    session_->send(event);
                }
            }
            watchFile();
        });
    };
    connect(watch_, &QFileSystemWatcher::fileChanged, this, diskChanged);
    /* Replacements/deletion can drop the file watch; the parent directory
     * lets us recover when the drawing is recreated. */
    connect(watch_, &QFileSystemWatcher::directoryChanged, this,
            [this, diskChanged](const QString &) { diskChanged(doc_->path()); });
    watchFile();
    applyTheme();
    updateTitle();
    resize(1400, 860);
}

MainWindow::~MainWindow() { delete session_; }

void MainWindow::placeWindow() {
    bool hypr = qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE");
    /* logical size of the screen the window will appear on */
    QSize screen;
    if (hypr) {
        const QJsonArray mons = QJsonDocument::fromJson(hyprctl({"monitors", "-j"})).array();
        for (const QJsonValue &v : mons) {
            QJsonObject m = v.toObject();
            if (!m["focused"].toBool()) continue;
            double scale = m["scale"].toDouble(1.0);
            bool rot = m["transform"].toInt() % 2 == 1;
            int w = m["width"].toInt(), h = m["height"].toInt();
            if (rot) std::swap(w, h);
            QJsonArray r = m["reserved"].toArray(); /* left, top, right, bottom (bar) */
            w = (int)(w / scale) - r.at(0).toInt() - r.at(2).toInt();
            h = (int)(h / scale) - r.at(1).toInt() - r.at(3).toInt();
            screen = QSize(w, h);
        }
    }
    QScreen *qs = QGuiApplication::primaryScreen();
    if (screen.isEmpty() && qs) screen = qs->availableGeometry().size();
    if (screen.isEmpty()) screen = QSize(1600, 1000);

    QString spec = settings_->get("window_size");
    QSize size(1400, 860);
    if (spec.endsWith('%')) {
        double f = spec.chopped(1).toDouble() / 100.0;
        size = QSize((int)(screen.width() * f), (int)(screen.height() * f));
    } else if (spec.contains('x')) {
        size = QSize(spec.section('x', 0, 0).toInt(), spec.section('x', 1, 1).toInt());
    }
    size = size.boundedTo(screen).expandedTo(QSize(640, 420));
    resize(size);

    bool fl = settings_->getBool("float");
    if (hypr) {
        /* With float=0 (default) any stale rule from an older session is
         * removed, so the window tiles like a normal window. With float=1 the
         * rule matches the main window only (its title ends in "fcai"), so
         * file dialogs keep Hyprland's normal dialog treatment. */
        QString lua = "if fcai_gui_rule then fcai_gui_rule:set_enabled(false) end; fcai_gui_rule = nil";
        if (fl)
            lua += QString("; fcai_gui_rule = hl.window_rule({ match = { class = \"^fcai-gui$\", "
                           "title = \".*fcai\" }, float = true, center = true, size = { %1, %2 } })")
                       .arg(size.width())
                       .arg(size.height());
        bool ok = false;
        hyprctl({"eval", lua}, &ok);
        if (!ok && fl) {
            /* Hyprland before the Lua config */
            QString m = "class:^(fcai-gui)$,title:^(.*fcai)$";
            hyprctl({"--batch", QString("keyword windowrulev2 float,%1 ; keyword windowrulev2 size %2 %3,%1 ; "
                                        "keyword windowrulev2 center,%1")
                                    .arg(m)
                                    .arg(size.width())
                                    .arg(size.height())});
        }
    } else if (fl && qs && QGuiApplication::platformName() != "wayland") {
        /* X11 and friends let the client pick its position */
        QRect g = qs->availableGeometry();
        move(g.center() - QPoint(size.width() / 2, size.height() / 2));
    }
}

void MainWindow::bringToFront() {
    raise();
    activateWindow();
    if (qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE") && settings_->getBool("float")) {
        /* the agent's terminal usually has focus; hand it to the drawing */
        QString pid = QString::number(QCoreApplication::applicationPid());
        bool ok = false;
        hyprctl({"dispatch", QString("hl.dsp.focus({ window = \"pid:%1\" })").arg(pid)}, &ok);
        if (!ok) hyprctl({"dispatch", "focuswindow", "pid:" + pid});
    }
}

void MainWindow::watchFile() {
    QStringList want;
    if (!doc_->path().isEmpty()) {
        QString dir = QFileInfo(doc_->path()).absolutePath();
        if (!watch_->directories().contains(dir)) watch_->addPath(dir);
    }
    if (!doc_->path().isEmpty() && QFile::exists(doc_->path())) want << doc_->path();
    if (settings_->get("theme") == "omarchy" && !Theme::omarchyColorsPath().isEmpty())
        want << Theme::omarchyColorsPath();
    for (const QString &p : want)
        if (!watch_->files().contains(p)) watch_->addPath(p);
}

void MainWindow::setAgentTitle(const QString &t) {
    title_ = t;
    updateTitle();
}

void MainWindow::updateTitle() {
    QString name = doc_->path().isEmpty() ? "untitled" : QFileInfo(doc_->path()).fileName();
    QString t = QString("%1%2 — fcai").arg(name, doc_->modified() ? " •" : "");
    if (!title_.isEmpty()) t = title_ + " · " + t;
    setWindowTitle(t);
}

/* No toolbar, no settings dialogs: the window is the canvas and an optional
 * bottom bar. Single keys live in Canvas::keyPressEvent (so they never fire
 * while typing in the message line); window-wide Ctrl shortcuts are here. */
void MainWindow::buildShortcuts() {
    auto key = [&](const QKeySequence &k, auto fn) {
        QAction *a = new QAction(this);
        a->setShortcut(k);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
        return a;
    };
    auto toggle = [&](const char *setting, const QKeySequence &k) {
        QString s = setting;
        return key(k, [this, s] {
            settings_->set(s, settings_->getBool(s) ? "0" : "1");
            settingChanged(s);
        });
    };
    toggle("palette", QKeySequence("Ctrl+B"));
    key(QKeySequence("Ctrl+Return"), [this] { done(QString()); });
    key(QKeySequence("Ctrl+W"), [this] { close(); });
    key(QKeySequence::Undo, [this] { doc_->undo(); });
    key(QKeySequence("Ctrl+Shift+Z"), [this] { doc_->redo(); });
    key(QKeySequence("Ctrl+Y"), [this] { doc_->redo(); });
    key(QKeySequence::SelectAll, [this] { canvas_->selectAll(); });
    key(QKeySequence::Copy, [this] { canvas_->copySelection(); });
    key(QKeySequence::Paste, [this] { canvas_->paste(); });
    key(QKeySequence::Cut, [this] {
        canvas_->copySelection();
        canvas_->deleteSelection();
    });
    key(QKeySequence("Ctrl+D"), [this] { canvas_->duplicateSelection(); });
    key(QKeySequence::Save, [this] { save(); });
}

void MainWindow::buildBar() {
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);
    info_ = new QLabel(bar);
    info_->setObjectName("info");
    info_->setTextFormat(Qt::RichText); /* the layer name is tinted with its colour */
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bar->addWidget(info_, 1);
    auto *hint = new QLabel("K  ·  Keybindings cheatsheet", bar);
    hint->setObjectName("keybindingsHint");
    hint->setContentsMargins(12, 0, 8, 0);
    bar->addPermanentWidget(hint);
    bar->setVisible(settings_->getBool("statusbar"));
}

QWidget *MainWindow::buildPalette() {
    auto *w = new QWidget(this);
    w->setObjectName("chrome");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(8, 8, 0, 8);
    w->setMinimumWidth(180);
    filter_ = new QLineEdit(w);
    filter_->setPlaceholderText("Search symbols…");
    filter_->setClearButtonEnabled(true);
    v->addWidget(filter_);
    tree_ = new QTreeWidget(w);
    tree_->setHeaderHidden(true);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setIndentation(10);
    v->addWidget(tree_, 1);
    preview_ = new QLabel(w);
    preview_->setObjectName("preview");
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFixedHeight(170);
    preview_->setToolTip("Preview of the selected symbol");
    v->addWidget(preview_);
    connect(filter_, &QLineEdit::textChanged, this, [this](const QString &t) { fillPalette(t); });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
        QString key = it->data(0, Qt::UserRole).toString();
        if (!key.isEmpty()) canvas_->setMacro(key);
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *, QTreeWidgetItem *) { updatePreview(); });
    fillPalette(QString());
    return w;
}

void MainWindow::fillPalette(const QString &filter) {
    tree_->clear();
    QStringList words = filter.toLower().split(' ', Qt::SkipEmptyParts);
    /* FidoCadJS hierarchy: library ([FIDOLIB name]) > category ({...}) > macro */
    QMap<QString, QTreeWidgetItem *> libs;
    QMap<QString, QTreeWidgetItem *> cats;
    int n = fc_lib_count(doc_->lib());
    for (int i = 0; i < n; i++) {
        const FcMacro *m = fc_lib_at(doc_->lib(), i);
        QString name = QString::fromUtf8(m->long_name ? m->long_name : m->key);
        QString cat = QString::fromUtf8(m->category && *m->category ? m->category : "Other");
        if (cat == "hidden") continue;
        QString lib = QString::fromUtf8(m->lib_name && *m->lib_name ? m->lib_name
                                                                   : (m->prefix ? m->prefix : ""));
        if (lib.isEmpty()) lib = "Other";
        QString hay = (name + " " + cat + " " + lib + " " + m->key).toLower();
        bool ok = true;
        for (const QString &w : words)
            if (!hay.contains(w)) ok = false;
        if (!ok) continue;
        QTreeWidgetItem *libItem = libs.value(lib);
        if (!libItem) {
            libItem = new QTreeWidgetItem(tree_, {lib});
            libItem->setFlags(Qt::ItemIsEnabled);
            libs[lib] = libItem;
        }
        QString catKey = lib + "\x1f" + cat;
        QTreeWidgetItem *catItem = cats.value(catKey);
        if (!catItem) {
            catItem = new QTreeWidgetItem(libItem, {cat});
            catItem->setFlags(Qt::ItemIsEnabled);
            cats[catKey] = catItem;
        }
        auto *it = new QTreeWidgetItem(catItem, {name});
        it->setData(0, Qt::UserRole, QString::fromUtf8(m->key));
        it->setToolTip(0, QString("%1\nkey %2 · %3").arg(name, m->key, lib));
    }
    if (!words.isEmpty()) tree_->expandAll();
    else tree_->collapseAll(); /* like FidoCadJS: libraries start collapsed */
}

void MainWindow::applyTheme() {
    *theme_ = Theme::load(settings_->get("theme"));
    theme_->apply(*app_, settings_->get("font"), settings_->getInt("font_size"));
    canvas_->refresh();
    updatePreview();
}

/* Render the macro selected in the library tree into the preview label. */
void MainWindow::updatePreview() {
    if (!preview_) return;
    QTreeWidgetItem *it = tree_ ? tree_->currentItem() : nullptr;
    QString key = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (!key.isEmpty()) preview_key_ = key;
    if (preview_key_.isEmpty()) {
        preview_->setPixmap(QPixmap());
        return;
    }
    qreal dpr = preview_->devicePixelRatioF();
    int w = qMax(80, (int)(preview_->width() * dpr));
    int h = qMax(80, (int)(preview_->height() * dpr));
    QImage img = macroPreview(doc_, preview_key_, w, h, canvas_->paper(), theme_->fg);
    if (img.isNull()) {
        preview_->setPixmap(QPixmap());
        return;
    }
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    preview_->setPixmap(pm);
}

void MainWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (preview_ && preview_->isVisible()) updatePreview();
}

void MainWindow::settingChanged(const QString &key) {
    if (key == "theme" || key == "font" || key == "font_size") {
        applyTheme();
        watchFile();
    } else if (key == "palette") {
        palette_->setVisible(settings_->getBool("palette"));
        if (settings_->getBool("palette"))
            updatePreview();
        else
            canvas_->setFocus();
    } else if (key == "statusbar") {
        statusBar()->setVisible(settings_->getBool("statusbar"));
        if (!settings_->getBool("statusbar")) canvas_->setFocus();
    } else if (key == "grid") {
        doc_->setGrid(settings_->getInt("grid"));
    }
    canvas_->refresh();
}

void MainWindow::docChanged() {
    canvas_->docChanged();
    if (settings_->getBool("autosave") && !doc_->path().isEmpty()) save();
    updateTitle();
}

bool MainWindow::save(const QString &what) {
    QString label = what.isEmpty() ? QString() : what + " ";
    if (doc_->path().isEmpty()) {
        QString p = QFileDialog::getSaveFileName(this, "Save drawing", QString(), "FidoCad (*.fcd)");
        if (p.isEmpty()) return false;
        QString err;
        if (!doc_->saveAs(p, &err)) {
            report(label + "save failed: " + err);
            return false;
        }
        watchFile();
    } else {
        QString err;
        bool ok = doc_->save(&err);
        if (!ok) {
            report(label + "save failed: " + err);
            return false;
        }
    }
    watchFile();
    updateTitle();
    disk_changed_ = false;
    return true;
}

QJsonObject MainWindow::stateEvent(const QString &event, const QString &note) {
    QJsonObject e;
    e["event"] = event;
    e["protocol"] = 2;
    e["note"] = note;
    e["file"] = doc_->path();
    e["saved"] = !doc_->path().isEmpty();
    e["modified"] = doc_->modified();
    e["disk_changed"] = disk_changed_;
    QJsonArray sel;
    for (int i : canvas_->selection()) sel << i;
    e["selection"] = sel;
    QJsonDocument diff = QJsonDocument::fromJson(doc_->diff(true).toUtf8());
    e["diff"] = diff.isObject() ? diff.object() : QJsonObject();
    e["fcd"] = QString::fromUtf8(doc_->fcd());
    FcDrawing previous;
    fc_drawing_init(&previous);
    fc_parse(&previous, previous_event_.constData(), (size_t)previous_event_.size());
    char *delta = fc_diff(&previous, &doc_->drawing(), 1);
    e["diff_prev"] = QJsonDocument::fromJson(delta ? QByteArray(delta) : QByteArray("{}")).object();
    free(delta);
    fc_drawing_free(&previous);
    previous_event_ = doc_->fcd();
    return e;
}

void MainWindow::sendToAgent(const QString &note) {
    if (!session_) {
        report("no agent session — start with --session-dir DIR");
        return;
    }
    QString text = note.trimmed();
    bool ok = true;
    QString err;
    if (!doc_->path().isEmpty()) {
        ok = doc_->save(&err);
        if (ok) disk_changed_ = false;
        updateTitle();
    }
    session_->send(stateEvent("send", text));
    canvas_->setFocus();
    if (!ok)
        report("sent to the agent, but saving failed: " + err);
    else
        report("drawing sent to the agent");
}

void MainWindow::toggleBar() {
    bool on = statusBar() && !statusBar()->isVisible();
    settings_->set("statusbar", on ? "1" : "0");
    settingChanged("statusbar");
    if (!on) canvas_->setFocus();
}

void MainWindow::applyCommand(const QJsonObject &c) {
    const QString cmd = c["cmd"].toString();
    QString error;
    bool reply = false;
    if (cmd == "push") {
        reply = true;
        if (!c["fcd"].isString()) error = "push requires fcd text";
        if (error.isEmpty() && c.contains("base")) {
            Document base;
            base.openText(c["base"].toString().toUtf8());
            if (!c["base"].isString() || base.fcd() != doc_->fcd())
                error = "drawing changed since base; read current state and merge";
        }
        if (error.isEmpty()) {
            doc_->setFcd(c["fcd"].toString().toUtf8());
            disk_changed_ = false;
            canvas_->fitIfOutside();
        }
    } else if (cmd == "save") {
        reply = true;
        if (doc_->path().isEmpty()) error = "no file name";
        else if (!save()) error = "could not save drawing";
    } else if (cmd == "reload") {
        reply = true;
        if (doc_->modified() && !c["force"].toBool())
            error = "unsaved GUI edits; merge first or explicitly use --force";
        else if (doc_->reloadFromDisk(&error)) {
            disk_changed_ = false;
            watchFile();
            canvas_->fitIfOutside();
        }
    } else if (cmd == "reload-libs") {
        reply = true;
        if (doc_->reloadLibraries(&error)) {
            fillPalette(filter_->text());
            updatePreview();
            canvas_->refresh();
            canvas_->fitIfOutside();
        }
    } else if (cmd == "set") {
        QString k = c["key"].toString(), v = c["value"].toString();
        QString e = settings_->set(k, v);
        if (!e.isEmpty())
            report("agent: " + e);
        else
            settingChanged(k);
    } else if (cmd == "ack") {
        /* the agent confirming it saw the user's drawing/edits */
        QString t = c["text"].toString().trimmed();
        report(t.isEmpty() ? QStringLiteral("agent acknowledged") : QStringLiteral("agent: ") + t);
    } else if (cmd == "get") {
        reply = true;
    } else if (cmd == "quit") {
        done(QString());
    }
    if (reply) {
        QJsonObject event = stateEvent(cmd == "get" ? "state" : "applied", QString());
        event["command"] = cmd;
        event["ok"] = error.isEmpty();
        if (!error.isEmpty()) event["error"] = error;
        if (c.contains("id")) event["id"] = c["id"];
        session_->send(event);
    }
}

bool MainWindow::barVisible() const { return statusBar() && statusBar()->isVisible(); }

void MainWindow::updateInfo(const QString &s) {
    if (info_) info_->setText(s);
}

void MainWindow::report(const QString &text) {
    if (text.isEmpty()) return;
    statusBar()->showMessage(text, 6000);
}

void MainWindow::done(const QString &note) {
    if (finishing_) return;
    status_ = "done";
    note_ = note.trimmed();
    if (!doc_->path().isEmpty()) {
        saved_ok_ = doc_->save(&save_err_);
    }
    finishing_ = true;
    close();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (!finishing_ && doc_->modified() && !doc_->path().isEmpty()) {
        auto r = QMessageBox::question(this, "Unsaved changes",
                                       "Save your changes before closing?",
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) {
            e->ignore();
            return;
        }
        if (r == QMessageBox::Save) {
            saved_ok_ = doc_->save(&save_err_);
        }
    }
    if (!finishing_) note_ = QString();
    if (session_) session_->send(stateEvent("closed", note_));
    finishing_ = true;
    e->accept();
    if (onFinished) onFinished();
}
