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
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
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

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
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
