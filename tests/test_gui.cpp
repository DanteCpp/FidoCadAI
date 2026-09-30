/* GUI smoke test: drives fcai-gui's widgets with synthetic mouse/keyboard
 * events on the offscreen platform, and exercises the agent session channel.
 * Run with QT_QPA_PLATFORM=offscreen. */

#include "../gui/document.h"
#include "../gui/props.h"
#include "../gui/session.h"
#include "../gui/settings.h"
#include "../gui/theme.h"
#include "../gui/window.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QSaveFile>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QScrollBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cstdio>

static int g_fail, g_ok;
#define EXPECT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
            g_fail++;                                                          \
        } else                                                                 \
            g_ok++;                                                            \
    } while (0)

static QString readAll(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

static void appendLine(const QString &path, const QString &line) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    f.write(line.toUtf8());
    f.write("\n");
    f.flush();
}

static QByteArray cli(const QStringList &args, int expected = 0) {
    QProcess p;
    p.start(QCoreApplication::applicationDirPath() + "/fcai", args);
    EXPECT(p.waitForStarted(2000));
    QElapsedTimer clock;
    clock.start();
    while (p.state() != QProcess::NotRunning && clock.elapsed() < 6000) QTest::qWait(10);
    if (p.state() != QProcess::NotRunning) { p.kill(); p.waitForFinished(); }
    EXPECT(p.exitStatus() == QProcess::NormalExit && p.exitCode() == expected);
    if (p.exitCode() != expected) fprintf(stderr, "%s\n", p.readAllStandardError().constData());
    return p.readAllStandardOutput();
}

static void test_session_feedback(QApplication &app, const char *argv0) {
    QTemporaryDir tmp;
    QString path = tmp.path() + "/d.fcd", libpath = tmp.path() + "/parts.fcl";
    QString dir = tmp.path() + "/session";
    auto write = [](const QString &p, const QByteArray &s) {
        QSaveFile f(p);
        EXPECT(f.open(QIODevice::WriteOnly));
        EXPECT(f.write(s) == s.size());
        EXPECT(f.commit());
    };
    write(path, "[FIDOCAD]\nMC 100 100 0 0 parts.part\n");
    write(libpath, "[part Part]\nLI 100 100 110 100 0\n");
    Settings settings(tmp.path() + "/gui.conf");
    Theme theme = Theme::load("dark");
    Document doc;
    doc.loadLibraries(false, {libpath}, {}, argv0);
    QString err;
    EXPECT(doc.open(path, &err));
    MainWindow win(&doc, &settings, &theme, &app, dir);
    win.show(); QTest::qWait(20);
    auto request = [&](const QString &cmd, const QStringList &extra = QStringList(), int code = 0) {
        return QJsonDocument::fromJson(cli(QStringList{"session", cmd, dir} + extra + QStringList{"--wait"}, code)).object();
    };
    /* Pending user sends are neither lost nor mistaken for correlated replies. */
    win.canvas()->onSend();
    QJsonObject status = QJsonDocument::fromJson(cli({"session", "status", dir})).object();
    EXPECT(status["pending_sends"].toInt() == 1);
    QJsonObject state = request("get");
    EXPECT(state["event"] == "state" && state["ok"].toBool());
    EXPECT(!state["id"].toString().isEmpty() && state["seq"].toInt() == 2);
    status = QJsonDocument::fromJson(cli({"session", "status", dir})).object();
    EXPECT(status["pending_sends"].toInt() == 1 && status["pending"].toInt() == 2);
    EXPECT(QJsonDocument::fromJson(cli({"session", "wait", dir, "--timeout", "1"})).object()["event"] == "send");

    /* Reload user libraries without changing FCD, selection, or undo history. */
    win.canvas()->setSelection({0});
    const QByteArray original = doc.fcd();
    write(libpath, "[part Part]\nLI 100 100 130 100 0\nFCJ 0 0 3 2 1 0\n[new Added]\nSA 100 100 0\n");
    EXPECT(request("reload-libs")["ok"].toBool());
    EXPECT(doc.fcd() == original && win.canvas()->selection() == QList<int>{0});
    EXPECT(!doc.canUndo());
    const FcMacro *macro = fc_lib_find(doc.lib(), "parts.part");
    EXPECT(macro && QByteArray(macro->body).contains("130 100"));
    EXPECT(fc_lib_find(doc.lib(), "parts.new") != nullptr);
    EXPECT(QFile::remove(libpath));
    EXPECT(!request("reload-libs", {}, 1)["ok"].toBool());
    EXPECT(fc_lib_find(doc.lib(), "parts.new") != nullptr); /* retain last good library */

    /* A same-content atomic replacement must not detach subsequent watching. */
    auto until = [](const std::function<bool()> &ready) {
        QElapsedTimer clock;
        clock.start();
        while (!ready() && clock.elapsed() < 2000) QTest::qWait(20);
        EXPECT(ready());
    };
    write(path, original); QTest::qWait(400);
    write(path, "LI 0 0 10 0 0\n");
    until([&] { return doc.primFcd(0) == "LI 0 0 10 0 0"; });
    EXPECT(doc.count() == 1 && doc.primFcd(0) == "LI 0 0 10 0 0");
    doc.append("SA 10 0 0\n");
    QByteArray dirty = doc.fcd();
    write(path, "LI 0 0 30 0 0\n");
    until([&] { return readAll(dir + "/events.jsonl").contains("\"event\":\"conflict\""); });
    EXPECT(doc.fcd() == dirty);
    EXPECT(readAll(dir + "/events.jsonl").contains("\"event\":\"conflict\""));
    state = request("get");
    EXPECT(state["disk_changed"].toBool() && state["modified"].toBool());
    EXPECT(!request("reload", {}, 1)["ok"].toBool());
    EXPECT(doc.fcd() == dirty);

    QString base = tmp.path() + "/base.fcd", draft = tmp.path() + "/draft.fcd";
    write(base, original); write(draft, "LI 0 0 40 0 0\n");
    EXPECT(!request("push", {draft, "--base", base}, 1)["ok"].toBool());
    EXPECT(doc.fcd() == dirty);
    write(base, dirty);
    state = request("push", {draft, "--base", base});
    EXPECT(state["ok"].toBool() && state["event"] == "applied");
    EXPECT(state["diff_prev"].toObject()["changes"].toInt() > 0);
    EXPECT(state["fcd"].toString().contains("40 0"));
    EXPECT(request("get")["diff_prev"].toObject()["changes"].toInt() == 0);
    EXPECT(request("save")["ok"].toBool());
    EXPECT(!doc.modified() && readAll(path).toUtf8() == doc.fcd());
    /* An agent write immediately after a save must not be lost to a timer. */
    write(path, "LI 0 0 50 0 0\n");
    until([&] { return doc.primFcd(0) == "LI 0 0 50 0 0"; });
    EXPECT(doc.primFcd(0) == "LI 0 0 50 0 0");
    EXPECT(QFile::remove(path)); QTest::qWait(300);
    write(path, "LI 0 0 60 0 0\n");
    until([&] { return doc.primFcd(0) == "LI 0 0 60 0 0"; });
    doc.append("SA 40 0 0\n");
    EXPECT(request("reload", {"--force"})["ok"].toBool());
    EXPECT(doc.count() == 1 && !doc.modified());

    /* Complete JSON lines can exceed the old 128 KiB reader buffer. */
    QString large = tmp.path() + "/large";
    QDir().mkpath(large);
    QJsonObject event{{"event", "send"}, {"fcd", QString(150000, 'x')}};
    QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    write(large + "/events.jsonl", line);
    EXPECT(cli({"session", "wait", large, "--timeout", "1"}) == line);
    cli({"session", "wait", large, "--timeout", "1"}, 3);
    /* Never send a guarded mutation to an old/unresponsive GUI that could
     * silently ignore --base. Only the read-only protocol probe is queued. */
    cli({"session", "push", large, draft, "--base", base, "--wait", "--timeout", "1"}, 3);
    EXPECT(!readAll(large + "/commands.jsonl").contains("\"cmd\":\"push\""));
    /* Restart archives logs and read offset, while starting a fresh queue. */
    QString archived = tmp.path() + "/archive";
    QDir().mkpath(archived);
    write(archived + "/events.jsonl", line);
    write(archived + "/commands.jsonl", "{\"cmd\":\"quit\"}\n");
    write(archived + "/.events.pos", "12");
    QObject owner;
    Session restarted(archived, &owner);
    EXPECT(restarted.start());
    QStringList history = QDir(archived + "/history").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    EXPECT(history.size() == 1);
    if (!history.isEmpty()) EXPECT(readAll(archived + "/history/" + history[0] + "/events.jsonl").toUtf8() == line);
    EXPECT(readAll(archived + "/events.jsonl").isEmpty());
    EXPECT(readAll(archived + "/commands.jsonl").isEmpty());
    EXPECT(!QFile::exists(archived + "/.events.pos"));
}

static void test_label_handles(const char *argv0) {
    QTemporaryDir tmp;
    Settings settings(tmp.path() + "/gui.conf");
    Theme theme = Theme::load("dark");
    Document doc;
    doc.loadLibraries(true, {}, {}, argv0);
    EXPECT(doc.openText("MC 100 100 1 1 080\nFCJ\n"
                        "TY 115 90 4 3 0 0 0 * R1\n"
                        "TY 115 110 4 3 0 0 0 * 10k\n"
                        "LI 60 70 80 70 0\n"));
    Canvas c(&doc, &settings, &theme);
    doc.onChanged = [&] { c.docChanged(); };
    c.resize(800, 600);
    c.show();
    c.fit();
    QTest::qWait(20);
    auto px = [&](QPointF p) { return c.toPixel(p).toPoint(); };
    auto drag = [&](QPointF from, QPointF to) {
        QTest::mousePress(&c, Qt::LeftButton, {}, px(from));
        QTest::mouseMove(&c, px((from + to) / 2));
        QTest::mouseMove(&c, px(to));
        QTest::mouseRelease(&c, Qt::LeftButton, {}, px(to));
    };
    /* Labels belong to the component for selection, but their handles move
     * independently even with another primitive selected. */
    QTest::mouseClick(&c, Qt::LeftButton, {}, px(QPointF(115, 90)));
    EXPECT(c.selection() == QList<int>{0});
    EXPECT(doc.handles(0) == 3);
    c.setSelection({0, 1});
    const QByteArray before = doc.fcd();
    drag(doc.handle(0, 1), QPointF(129.6, 80.2));
    EXPECT(doc.handle(0, 1) == QPointF(130, 80));
    EXPECT(doc.handle(0, 2) == QPointF(115, 110));
    EXPECT(doc.handle(0, 0) == QPointF(100, 100));
    EXPECT(doc.primFcd(0).startsWith("MC 100 100 1 1 080"));
    EXPECT(doc.primFcd(1) == "LI 60 70 80 70 0");
    const QByteArray nameMoved = doc.fcd();
    doc.undo();
    EXPECT(doc.fcd() == before); /* the entire drag is a single undo */
    doc.redo();
    EXPECT(doc.fcd() == nameMoved);
    drag(doc.handle(0, 2), QPointF(134.6, 120.2));
    EXPECT(doc.handle(0, 2) == QPointF(135, 120));
    EXPECT(doc.handle(0, 1) == QPointF(130, 80));
    EXPECT(doc.handle(0, 0) == QPointF(100, 100));
    EXPECT(doc.primFcd(1) == "LI 60 70 80 70 0");
    const QByteArray bothMoved = doc.fcd();
    doc.undo();
    EXPECT(doc.fcd() == nameMoved);
    doc.redo();
    EXPECT(doc.fcd() == bothMoved);

    /* Ordinary body dragging still carries the repositioned labels. */
    c.setSelection({0});
    drag(QPointF(100, 105), QPointF(110, 110));
    EXPECT(doc.handle(0, 0) == QPointF(110, 105));
    EXPECT(doc.handle(0, 1) == QPointF(140, 85));
    EXPECT(doc.handle(0, 2) == QPointF(145, 125));
    QString err;
    EXPECT(doc.saveAs(tmp.path() + "/labels.fcd", &err));
    Document reopened;
    EXPECT(reopened.open(doc.path(), &err));
    EXPECT(reopened.fcd() == doc.fcd());
    EXPECT(reopened.handle(0, 1) == QPointF(140, 85));
    EXPECT(reopened.handle(0, 2) == QPointF(145, 125));
}

static void test_grouped_handles(const char *argv0) {
    QTemporaryDir tmp;
    Settings settings(tmp.path() + "/gui.conf");
    Theme theme = Theme::load("dark");
    Document doc;
    doc.loadLibraries(true, {}, {}, argv0);
    EXPECT(doc.openText("MC 60 100 0 0 080\nFCJ\n"
                        "TY 80 50 4 3 0 0 0 * R1\n"
                        "TY 80 65 4 3 0 0 0 * 10k\n"
                        "MC 140 100 1 1 080\nFCJ\n"
                        "TY 160 50 4 3 0 0 0 * R2\n"
                        "TY 160 65 4 3 0 0 0 * 20k\n"));
    Canvas c(&doc, &settings, &theme);
    doc.onChanged = [&] { c.docChanged(); };
    c.resize(1000, 600);
    c.show();
    c.fit();
    QTest::qWait(20);
    auto px = [&](double x, double y) { return c.toPixel(QPointF(x, y)).toPoint(); };
    auto drag = [&](QPoint from, QPoint to) {
        QTest::mousePress(&c, Qt::LeftButton, {}, from);
        QTest::mouseMove(&c, (from + to) / 2);
        QTest::mouseMove(&c, to);
        QTest::mouseRelease(&c, Qt::LeftButton, {}, to);
    };
    auto bodiesUnchanged = [&] {
        EXPECT(doc.primFcd(0).startsWith("MC 60 100 0 0 080"));
        EXPECT(doc.primFcd(1).startsWith("MC 140 100 1 1 080"));
    };
    const QByteArray original = doc.fcd();
    /* Box-select both name/value pairs, with no component body enclosed. */
    drag(px(75, 45), px(185, 75));
    EXPECT((c.selection() == QList<int>{0, 1}));
    EXPECT(doc.fcd() == original && !doc.canUndo());
    drag(px(80, 50), px(89.6, 55.2));
    EXPECT(doc.handle(0, 1) == QPointF(90, 55));
    EXPECT(doc.handle(0, 2) == QPointF(90, 70));
    EXPECT(doc.handle(1, 1) == QPointF(170, 55));
    EXPECT(doc.handle(1, 2) == QPointF(170, 70));
    bodiesUnchanged();
    const QByteArray moved = doc.fcd();
    /* The group stays selected for repeated nudges. */
    QTest::keyClick(&c, Qt::Key_Right);
    QTest::keyClick(&c, Qt::Key_Right);
    EXPECT(doc.handle(0, 1) == QPointF(100, 55));
    EXPECT(doc.handle(1, 2) == QPointF(180, 70));
    bodiesUnchanged();
    doc.undo(); doc.undo();
    EXPECT(doc.fcd() == moved);
    doc.undo();
    EXPECT(doc.fcd() == original);
    doc.redo();
    EXPECT(doc.fcd() == moved);
    doc.undo();

    /* Ctrl-click chooses exactly the desired handles; deselection works too. */
    c.setSelection({0, 1});
    QTest::mouseClick(&c, Qt::LeftButton, Qt::ControlModifier, px(80, 50));
    QTest::mouseClick(&c, Qt::LeftButton, Qt::ControlModifier, px(80, 65));
    QTest::mouseClick(&c, Qt::LeftButton, Qt::ControlModifier, px(160, 50));
    QTest::mouseClick(&c, Qt::LeftButton, Qt::ControlModifier, px(160, 50));
    EXPECT(doc.fcd() == original);
    drag(px(80, 65), px(90, 75));
    EXPECT(doc.handle(0, 1) == QPointF(90, 60));
    EXPECT(doc.handle(0, 2) == QPointF(90, 75));
    EXPECT(doc.handle(1, 1) == QPointF(160, 50));
    EXPECT(doc.handle(1, 2) == QPointF(160, 65));
    bodiesUnchanged();
    /* Adding another pair with Ctrl+box preserves the first pair. */
    QTest::mousePress(&c, Qt::LeftButton, Qt::ControlModifier, px(155, 45));
    QTest::mouseMove(&c, px(185, 75));
    QTest::mouseRelease(&c, Qt::LeftButton, Qt::ControlModifier, px(185, 75));
    c.nudge(-1000, 0);
    EXPECT(doc.handle(0, 1) == QPointF(0, 60));
    EXPECT(doc.handle(0, 2) == QPointF(0, 75));
    EXPECT(doc.handle(1, 1) == QPointF(70, 50));
    EXPECT(doc.handle(1, 2) == QPointF(70, 65));
    bodiesUnchanged();
    /* New document contents invalidate old handle indices. */
    doc.setFcd("MC 60 100 0 0 080\n");
    c.nudge(5, 0);
    EXPECT(doc.primFcd(0) == "MC 65 100 0 0 080");
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    test_label_handles(argv[0]);
    test_grouped_handles(argv[0]);
    test_session_feedback(app, argv[0]);
    QTemporaryDir tmp;
    QString fcd = tmp.path() + "/t.fcd";
    {
        QFile f(fcd);
        EXPECT(f.open(QIODevice::WriteOnly));
        f.write("[FIDOCAD]\nLI 10 10 30 10 0\n");
    }
    Settings settings(tmp.path() + "/gui.conf");
    Theme theme = Theme::load("dark");
    Document doc;
    doc.loadLibraries(true, {}, {}, argv[0]);
    QString err;
    EXPECT(doc.open(fcd, &err));
    QString session_dir = tmp.path() + "/session";
    MainWindow win(&doc, &settings, &theme, &app, session_dir);
    win.resize(1200, 800);
    win.show();
    QTest::qWait(50);
    Canvas *c = win.canvas();
    auto px = [c](double x, double y) { return c->toPixel(QPointF(x, y)).toPoint(); };

    /* line tool: two clicks, snapped to the 5-unit grid */
    c->setTool(Canvas::Line);
    QTest::mouseClick(c, Qt::LeftButton, {}, px(10.4, 30.2));
    QTest::mouseClick(c, Qt::LeftButton, {}, px(39.6, 30.1));
    QTest::keyClick(c, Qt::Key_Escape);
    EXPECT(doc.count() == 2);
    EXPECT(doc.primFcd(1) == "LI 10 30 40 30 0");

    /* select tool: click the new line and drag it by (10, 5) */
    c->setTool(Canvas::Select);
    QTest::mousePress(c, Qt::LeftButton, {}, px(25, 30));
    EXPECT(c->selection() == QList<int>{1});
    QTest::mouseMove(c, px(30, 32));
    QTest::mouseMove(c, px(35, 35));
    QTest::mouseRelease(c, Qt::LeftButton, {}, px(35, 35));
    EXPECT(doc.primFcd(1) == "LI 20 35 50 35 0");

    /* Shift+click still toggles the selection; Shift+drag measures */
    c->setSelection({});
    QTest::mousePress(c, Qt::LeftButton, Qt::ShiftModifier, px(25, 35));
    QTest::mouseRelease(c, Qt::LeftButton, Qt::ShiftModifier, px(25, 35));
    EXPECT(c->selection() == QList<int>{1});
    c->setSelection({});
    QTest::mousePress(c, Qt::LeftButton, Qt::ShiftModifier, px(10, 10));
    QTest::mouseMove(c, px(60, 10));
    QTest::mouseRelease(c, Qt::LeftButton, Qt::ShiftModifier, px(60, 10));
    EXPECT(c->rulerVisible() && c->rulerLength() > 0);
    QTest::mousePress(c, Qt::LeftButton, {}, px(90, 90));
    QTest::mouseRelease(c, Qt::LeftButton, {}, px(90, 90));
    EXPECT(!c->rulerVisible());
    c->setSelection({1}); /* the handle drag below needs it selected */

    /* control-point handle: drag the line's end point to (55,40) */
    EXPECT(doc.handles(1) == 2);
    EXPECT(doc.handle(1, 1) == QPointF(50, 35));
    QTest::mousePress(c, Qt::LeftButton, {}, px(50, 35));
    QTest::mouseMove(c, px(54.6, 40.2));
    QTest::mouseRelease(c, Qt::LeftButton, {}, px(54.6, 40.2));
    EXPECT(doc.primFcd(1) == "LI 20 35 55 40 0");
    doc.undo();
    EXPECT(doc.primFcd(1) == "LI 20 35 50 35 0");
    doc.redo();
    EXPECT(doc.primFcd(1) == "LI 20 35 55 40 0");
    doc.undo();

    /* rectangle by dragging */
    c->setTool(Canvas::Rect);
    QTest::mousePress(c, Qt::LeftButton, {}, px(60, 60));
    QTest::mouseMove(c, px(70, 65));
    QTest::mouseMove(c, px(80, 70));
    QTest::mouseRelease(c, Qt::LeftButton, {}, px(80, 70));
    EXPECT(doc.count() == 3 && doc.primFcd(2) == "RV 60 60 80 70 0");

    /* place a resistor, rotate it, give it a name */
    c->setMacro("080");
    QTest::mouseClick(c, Qt::LeftButton, {}, px(100, 50));
    EXPECT(doc.count() == 4 && doc.primFcd(3).startsWith("MC 100 50 0 0 080"));
    c->setTool(Canvas::Select);
    c->setSelection({3});
    c->rotateSelection();
    EXPECT(doc.primFcd(3).startsWith("MC 100 50 1 0 080"));
    doc.setText(3, 1, "R1");
    EXPECT(doc.primFcd(3).contains("R1"));

    /* rubber-band selection, delete, undo */
    c->setSelection({});
    QTest::mousePress(c, Qt::LeftButton, {}, px(55, 55));
    QTest::mouseMove(c, px(70, 70));
    QTest::mouseMove(c, px(85, 75));
    QTest::mouseRelease(c, Qt::LeftButton, {}, px(85, 75));
    EXPECT(c->selection() == QList<int>{2});
    c->deleteSelection();
    EXPECT(doc.count() == 3);
    doc.undo();
    EXPECT(doc.count() == 4);

    /* tools are keyboard-only: L switches to line, Esc back to select */
    c->setFocus();
    QTest::keyClick(c, Qt::Key_L);
    EXPECT(c->tool() == Canvas::Line);
    QTest::keyClick(c, Qt::Key_Escape);
    EXPECT(c->tool() == Canvas::Select);

    /* K opens the shortcut sheet; typing filters it (Omarchy-style search) */
    QTest::keyClick(c, Qt::Key_K);
    EXPECT(c->cheatsheetVisible());
    EXPECT(c->cheatsheetScroll() == 0);
    QTest::keyClicks(c, "grid");
    EXPECT(c->cheatsheetQuery() == "grid");
    QTest::keyClick(c, Qt::Key_Backspace);
    EXPECT(c->cheatsheetQuery() == "gri");
    /* the expanded sheet scrolls with the arrow keys and the wheel */
    QTest::keyClick(c, Qt::Key_Escape);
    QTest::keyClick(c, Qt::Key_K);
    QTest::keyClick(c, Qt::Key_Down);
    QTest::keyClick(c, Qt::Key_Down);
    EXPECT(c->cheatsheetScroll() > 0);
    QTest::keyClick(c, Qt::Key_Home);
    EXPECT(c->cheatsheetScroll() == 0);
    QTest::keyClick(c, Qt::Key_End);
    EXPECT(c->cheatsheetScroll() > 0);
    QTest::keyClick(c, Qt::Key_Escape);
    EXPECT(!c->cheatsheetVisible());
    QTest::keyClick(c, Qt::Key_K);
    EXPECT(c->cheatsheetVisible() && c->cheatsheetQuery().isEmpty() && c->cheatsheetScroll() == 0);
    QTest::keyClick(c, Qt::Key_K);
    EXPECT(!c->cheatsheetVisible());

    /* The sheet has a usable scrollbar and consumes pointer input. */
    QTest::keyClick(c, Qt::Key_K);
    c->repaint();
    auto *sheetBar = c->findChild<QScrollBar *>("cheatsheetScrollBar");
    EXPECT(sheetBar && sheetBar->isVisible() && sheetBar->maximum() > 0);
    if (sheetBar) {
        const QByteArray beforeSheet = doc.fcd();
        const QPointF beforeZoom = c->toPixel(QPointF(100, 100));
        QTest::mouseClick(c, Qt::LeftButton, {}, c->rect().center());
        QTest::mouseDClick(c, Qt::LeftButton, {}, c->rect().center());
        EXPECT(c->cheatsheetVisible() && !c->editing());
        QWheelEvent wheel(c->rect().center(), c->mapToGlobal(c->rect().center()),
                          QPoint(0, -35), QPoint(), Qt::NoButton, Qt::NoModifier,
                          Qt::ScrollUpdate, false);
        QApplication::sendEvent(c, &wheel);
        c->repaint();
        EXPECT(c->cheatsheetScroll() == 35);
        EXPECT(c->toPixel(QPointF(100, 100)) == beforeZoom);
        QTest::keyClick(c, Qt::Key_Home);
        c->repaint();
        QTest::mouseClick(sheetBar, Qt::LeftButton, {}, QPoint(4, sheetBar->height() - 30));
        c->repaint();
        EXPECT(c->cheatsheetVisible() && c->cheatsheetScroll() > 0);
        QTest::keyClick(c, Qt::Key_End);
        c->repaint();
        EXPECT(c->cheatsheetScroll() == sheetBar->maximum());
        QTest::keyClicks(c, "grid");
        c->repaint();
        EXPECT(c->cheatsheetScroll() == 0 && !sheetBar->isVisible());
        EXPECT(doc.fcd() == beforeSheet);
    }
    QTest::keyClick(c, Qt::Key_Escape);
    c->repaint();
    EXPECT(sheetBar && !sheetBar->isVisible());

    /* ruler: right-drag measures, the result stays until the next press */
    QTest::mousePress(c, Qt::RightButton, {}, px(10, 10));
    QTest::mouseMove(c, px(40, 10));
    QTest::mouseRelease(c, Qt::RightButton, {}, px(40, 10));
    EXPECT(c->rulerVisible());
    EXPECT(c->rulerLength() > 0);
    QTest::mousePress(c, Qt::LeftButton, {}, px(90, 90));
    EXPECT(!c->rulerVisible());
    QTest::mouseRelease(c, Qt::LeftButton, {}, px(90, 90));

    /* the bottom bar is hidden by default; I toggles it and persists */
    EXPECT(!win.barVisible());
    EXPECT(settings.get("statusbar") == "0");
    QTest::keyClick(c, Qt::Key_I);
    EXPECT(win.barVisible());
    EXPECT(settings.get("statusbar") == "1");
    auto *keyHint = win.findChild<QLabel *>("keybindingsHint");
    EXPECT(keyHint && keyHint->isVisible() && keyHint->text().contains("K") &&
           keyHint->text().contains("Keybindings cheatsheet"));
    QTest::keyClick(c, Qt::Key_I);
    EXPECT(!win.barVisible());
    EXPECT(settings.get("statusbar") == "0");

    /* Return on the canvas sends the drawing to the agent, GUI stays open */
    QTest::keyClick(c, Qt::Key_Return);
    QTest::qWait(20);
    {
        QString ev = readAll(session_dir + "/events.jsonl");
        EXPECT(ev.contains("\"event\":\"send\""));
        EXPECT(ev.contains("\"fcd\""));
        EXPECT(ev.contains("\"diff\""));
        EXPECT(win.status() == "closed");
    }

    /* Return fires through onSend (hook swapped so nothing is saved) */
    {
        auto keep = c->onSend;
        bool fired = false;
        c->onSend = [&fired] { fired = true; };
        QTest::keyClick(c, Qt::Key_Return);
        EXPECT(fired);
        c->onSend = keep;
        EXPECT(win.status() == "closed");
    }

    /* agent -> GUI: push / set / get through commands.jsonl */
    appendLine(session_dir + "/commands.jsonl", R"({"cmd":"push","fcd":"RV 50 50 60 60 0\n"})");
    win.session()->pollNow();
    EXPECT(doc.count() == 1); /* push replaces the drawing */
    EXPECT(doc.primFcd(0) == "RV 50 50 60 60 0");
    auto drawingVisible = [&] {
        QRectF b = doc.boundsAll();
        QRectF viewport(0, 0, c->width(), c->height());
        return viewport.contains(c->toPixel(b.topLeft())) &&
               viewport.contains(c->toPixel(b.bottomRight()));
    };
    auto push = [&](const QString &text) {
        QJsonObject cmd{{"cmd", "push"}, {"fcd", text}};
        appendLine(session_dir + "/commands.jsonl",
                   QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
        win.session()->pollNow();
    };
    EXPECT(drawingVisible());

    /* Fitting follows window-manager resizes, including a delayed initial tile. */
    push("RV 100 100 400 300 0\n");
    c->fit();
    win.resize(640, 420);
    QTest::qWait(20);
    EXPECT(drawingVisible());
    QRectF fittedBounds = doc.boundsAll();
    EXPECT((c->toPixel(fittedBounds.center()) - QPointF(c->width() / 2.0,
                                                      c->height() / 2.0)).manhattanLength() < 1);
    win.resize(1200, 800);
    QTest::qWait(20);
    EXPECT((c->toPixel(fittedBounds.center()) - QPointF(c->width() / 2.0,
                                                      c->height() / 2.0)).manhattanLength() < 1);
    /* A deliberately zoomed view must survive resizes until Space restores fit. */
    c->zoomBy(2, QPointF(100, 100));
    QPointF manualOrigin = c->toPixel(QPointF(0, 0));
    QPointF manualUnit = c->toPixel(QPointF(1, 1));
    win.resize(900, 600);
    QTest::qWait(20);
    EXPECT(c->toPixel(QPointF(0, 0)) == manualOrigin);
    EXPECT(c->toPixel(QPointF(1, 1)) == manualUnit);
    QTest::keyClick(c, Qt::Key_Space);
    win.resize(640, 420);
    QTest::qWait(20);
    EXPECT(drawingVisible());
    QTest::mousePress(c, Qt::MiddleButton, {}, QPoint(200, 150));
    QTest::mouseMove(c, QPoint(230, 170));
    QTest::mouseRelease(c, Qt::MiddleButton, {}, QPoint(230, 170));
    manualOrigin = c->toPixel(QPointF(0, 0));
    manualUnit = c->toPixel(QPointF(1, 1));
    win.resize(1200, 800);
    QTest::qWait(20);
    EXPECT(c->toPixel(QPointF(0, 0)) == manualOrigin);
    EXPECT(c->toPixel(QPointF(1, 1)) == manualUnit);

    /* Agent additions outside any edge refit the whole drawing. */
    for (const QString &addition : {QString("RV 1500 1000 1510 1010 0\n"),
                                    QString("RV 1000 1500 1010 1510 0\n"),
                                    QString("RV 0 1000 10 1010 0\n"),
                                    QString("RV 1000 0 1010 10 0\n")}) {
        push("RV 1000 1000 1010 1010 0\n");
        c->fit();
        QPointF before = c->toPixel(QPointF(0, 0));
        push(QString::fromUtf8(doc.fcd()) + addition);
        EXPECT(drawingVisible());
        EXPECT(c->toPixel(QPointF(0, 0)) != before);
    }

    /* In-view and empty updates preserve both pan and zoom. */
    QPointF origin = c->toPixel(QPointF(0, 0));
    QPointF unit = c->toPixel(QPointF(1, 1));
    push(QString::fromUtf8(doc.fcd()) + "SA 1005 1005 0\n");
    EXPECT(c->toPixel(QPointF(0, 0)) == origin);
    EXPECT(c->toPixel(QPointF(1, 1)) == unit);
    push("[FIDOCAD]\n");
    EXPECT(doc.count() == 0);
    EXPECT(c->toPixel(QPointF(0, 0)) == origin);
    EXPECT(c->toPixel(QPointF(1, 1)) == unit);

    /* Local edits do not move the view, even outside it. */
    doc.append("RV 3000 3000 3010 3010 0\n");
    EXPECT(!drawingVisible());
    EXPECT(c->toPixel(QPointF(0, 0)) == origin);
    EXPECT(c->toPixel(QPointF(1, 1)) == unit);
    /* A large drawing must fit even below the manual zoom minimum. */
    push("RV 0 0 10000 10000 0\n");
    EXPECT(drawingVisible());
    push("RV 1000 1000 1010 1010 0\n");
    c->fit(); /* the disk update below is now well outside the view */
    appendLine(session_dir + "/commands.jsonl", R"({"cmd":"set","key":"grid","value":"10"})");
    win.session()->pollNow();
    EXPECT(settings.get("grid") == "10");
    appendLine(session_dir + "/commands.jsonl", R"({"cmd":"get"})");
    win.session()->pollNow();
    QTest::qWait(20);
    EXPECT(readAll(session_dir + "/events.jsonl").contains("\"event\":\"state\""));

    /* the agent editing the file on disk updates the canvas live */
    {
        QString save_err;
        EXPECT(doc.save(&save_err));
        QTest::qWait(450); /* let the window forget its own write first */
        QFile f(fcd);
        EXPECT(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[FIDOCAD]\nLI 1 5 9 5 0\nRV 2 6 8 9 0\n");
        f.close();
        QTest::qWait(700);
        EXPECT(doc.count() == 2);
        EXPECT(doc.primFcd(0) == "LI 1 5 9 5 0");
        EXPECT(doc.primFcd(1) == "RV 2 6 8 9 0");
        EXPECT(drawingVisible());
        QPointF origin = c->toPixel(QPointF(0, 0));
        QPointF unit = c->toPixel(QPointF(1, 1));
        EXPECT(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[FIDOCAD]\nLI 1 5 9 5 0\nRV 2 6 8 9 1\n");
        f.close();
        QTest::qWait(700);
        EXPECT(doc.primFcd(1) == "RV 2 6 8 9 1");
        EXPECT(c->toPixel(QPointF(0, 0)) == origin);
        EXPECT(c->toPixel(QPointF(1, 1)) == unit);
    }

    /* properties form inside the canvas: arrows + dash on the line */
    c->setSelection({0});
    QTest::keyClick(c, Qt::Key_P);
    EXPECT(c->editing());
    InlineForm *form = c->form();
    EXPECT(form != nullptr);
    if (form) {
        QList<QComboBox *> combos = form->findChildren<QComboBox *>();
        EXPECT(combos.size() == 4); /* Layer, Dash, Arrows, Arrow style */
        if (combos.size() == 4) {
            combos[1]->setCurrentIndex(1); /* dashed */
            combos[2]->setCurrentIndex(2); /* arrow at end */
        }
        QList<QLineEdit *> edits = form->findChildren<QLineEdit *>();
        EXPECT(edits.size() >= 3 && edits[0]->hasFocus()); /* Name first */
        /* typing letters in a field must not trigger canvas tools */
        QTest::keyClicks(edits[0], "Lk");
        EXPECT(c->tool() == Canvas::Select && edits[0]->text() == "Lk");
        edits[0]->clear();
        QTest::keyClick(edits[0], Qt::Key_Return);
    }
    QTest::qWait(10);
    EXPECT(!c->editing());
    EXPECT(doc.primFcd(0).contains("FCJ 2 0 3 1 1 0"));

    /* bad input keeps the form open with the error */
    c->setSelection({0});
    c->editSelection();
    form = c->form();
    if (form) {
        QList<QLineEdit *> edits = form->findChildren<QLineEdit *>();
        edits[2]->setText("1 2 3"); /* Coordinates */
        form->apply();
        EXPECT(c->editing());
        QTest::keyClick(edits[2], Qt::Key_Escape);
    }
    QTest::qWait(10);
    EXPECT(!c->editing());

    /* text tool: click, type, Enter */
    int nt = doc.count();
    c->setTool(Canvas::Text);
    QTest::mouseClick(c, Qt::LeftButton, {}, px(10, 80));
    EXPECT(c->editing());
    if (QLineEdit *t = c->form() ? c->form()->findChild<QLineEdit *>() : nullptr) {
        QTest::keyClicks(t, "Vout");
        QTest::keyClick(t, Qt::Key_Return);
    }
    QTest::qWait(10);
    EXPECT(doc.count() == nt + 1 && doc.primFcd(nt).endsWith("Vout"));
    doc.undo();
    c->setTool(Canvas::Select);

    /* the library panel toggles with Ctrl+B and persists */
    EXPECT(settings.get("palette") == "0");
    QTest::keyClick(c, Qt::Key_B, Qt::ControlModifier);
    EXPECT(settings.get("palette") == "1");

    /* the library tree is library > category > macro, and has a preview */
    {
        QTreeWidget *tree = win.findChild<QTreeWidget *>();
        EXPECT(tree != nullptr);
        QTreeWidgetItem *std = nullptr;
        if (tree) {
            for (int i = 0; i < tree->topLevelItemCount(); i++)
                if (tree->topLevelItem(i)->text(0) == "Standard library")
                    std = tree->topLevelItem(i);
        }
        EXPECT(std != nullptr);
        QTreeWidgetItem *cat = nullptr, *mac = nullptr;
        if (std) {
            for (int i = 0; i < std->childCount(); i++)
                if (std->child(i)->text(0) == "Basic symbols") cat = std->child(i);
        }
        EXPECT(cat != nullptr);
        if (cat && cat->childCount() > 0) mac = cat->child(0);
        EXPECT(mac != nullptr && mac->data(0, Qt::UserRole).toString() == "000");
        QLabel *preview = win.findChild<QLabel *>("preview");
        EXPECT(preview != nullptr);
        if (tree && mac) tree->setCurrentItem(mac);
        QTest::qWait(10);
        EXPECT(preview && !preview->pixmap().isNull());
    }

    /* the info bar is state only: no shortcut hints, layer tinted with its colour */
    {
        QLabel *info = win.findChild<QLabel *>("info");
        EXPECT(info != nullptr);
        QTest::mouseMove(c, px(30, 30));
        QTest::qWait(10);
        if (info) {
            EXPECT(info->text().contains("layer"));
            EXPECT(!info->text().contains("click points"));
            EXPECT(!info->text().contains("right-click/Esc"));
        }
    }

    /* agent acknowledgement lands in the bottom bar */
    appendLine(session_dir + "/commands.jsonl", R"({"cmd":"ack","text":"got your edit"})");
    win.session()->pollNow();
    QTest::qWait(10);
    EXPECT(win.statusBar()->currentMessage().contains("agent: got your edit"));

    QTest::keyClick(c, Qt::Key_B, Qt::ControlModifier);
    EXPECT(settings.get("palette") == "0");

    /* settings are plain key/values now (no chat): validation still applies */
    EXPECT(settings.set("grid", "10").isEmpty());
    EXPECT(settings.get("grid") == "10");
    EXPECT(!settings.set("grid", "999").isEmpty());
    EXPECT(settings.get("grid") == "10");
    EXPECT(!settings.set("theme", "purple").isEmpty());
    EXPECT(settings.set("snap", "off").isEmpty());
    EXPECT(settings.get("snap") == "0");
    EXPECT(settings.set("snap", "on").isEmpty());
    EXPECT(!settings.set("window_size", "200%").isEmpty());
    EXPECT(settings.dump(false).contains("statusbar"));

    /* Ctrl+Return is Done: saves the drawing and reports it */
    QTest::keyClick(c, Qt::Key_Return, Qt::ControlModifier);
    QTest::qWait(50);
    EXPECT(win.status() == "done");
    EXPECT(win.saved());
    EXPECT(readAll(session_dir + "/events.jsonl").contains("\"event\":\"closed\""));

    printf("gui: %d passed, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
