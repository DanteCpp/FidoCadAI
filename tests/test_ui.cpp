/* End-to-end editor interactions: input goes through the top-level QWindow,
 * including Qt's hit testing, focus, shortcuts and double-click dispatch.
 * Document/Canvas access below is for fixture setup and assertions only. */
#include "../gui/window.h"
#include "../gui/props.h"
#include "../gui/keysheet.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cstdio>

static int passed, failed;
#define CHECK(c) do { if (c) ++passed; else { ++failed; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Editor {
    QTemporaryDir tmp;
    Settings settings{tmp.path() + "/gui.conf"};
    Theme theme = Theme::load("dark");
    Document doc;
    MainWindow win;
    Canvas *c;

    Editor(QApplication &app, const QByteArray &fcd = "[FIDOCAD]\n")
        : win(prepare(fcd), &settings, &theme, &app, {}), c(win.canvas()) {
        win.resize(1000, 700);
        win.show();
        win.activateWindow();
        QTest::qWait(30);
    }
    Document *prepare(const QByteArray &fcd) {
        QFile file(tmp.path() + "/drawing.fcd");
        CHECK(file.open(QIODevice::WriteOnly));
        file.write(fcd);
        file.close();
        CHECK(doc.loadLibraries(true, {}, {}));
        CHECK(doc.open(file.fileName(), nullptr));
        return &doc;
    }
    QPoint point(QPointF logical) const { return c->mapTo(&win, c->toPixel(logical).toPoint()); }
    void key(Qt::Key k, Qt::KeyboardModifiers mods = {}) {
        QTest::keyClick(win.windowHandle(), k, mods);
        QApplication::processEvents();
    }
    void click(QPointF logical, Qt::MouseButton button = Qt::LeftButton,
               Qt::KeyboardModifiers mods = {}) {
        QTest::mouseClick(win.windowHandle(), button, mods, point(logical));
        QApplication::processEvents();
    }
    void move(QPointF logical) {
        QTest::mouseMove(win.windowHandle(), point(logical));
        QApplication::processEvents();
    }
    void mouseEvent(QEvent::Type type, QPointF logical, Qt::MouseButton button,
                    Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = {}) {
        QPoint pos = point(logical);
        QMouseEvent event(type, pos, win.mapToGlobal(pos), button, buttons, mods);
        QApplication::sendEvent(win.windowHandle(), &event);
        QApplication::processEvents();
    }
    void drag(QPointF from, QPointF to, bool intermediate = true,
              Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers mods = {}) {
        QTest::mousePress(win.windowHandle(), button, mods, point(from));
        if (intermediate) QTest::mouseMove(win.windowHandle(), point((from + to) / 2));
        // QTest::mouseRelease(QWindow*) inserts an extra move to the release
        // position. Real input may coalesce moves; send that release itself.
        mouseEvent(QEvent::MouseButtonRelease, to, button, Qt::NoButton, mods);
        QApplication::processEvents();
    }
    void expectPrim(int i, const QString &expected) {
        if (doc.primFcd(i) != expected)
            fprintf(stderr, "expected [%s], got [%s]\n", qPrintable(expected), qPrintable(doc.primFcd(i)));
        CHECK(doc.primFcd(i) == expected);
    }
};

static void test_final_pointer_position(QApplication &app) {
    Editor e(app);
    e.key(Qt::Key_L);
    e.click({20, 30});
    e.click({60, 30});
    /* The second press of a double-click can land on the next grid point.
     * QWidget dispatches it as MouseButtonDblClick, not mousePressEvent. */
    e.mouseEvent(QEvent::MouseButtonDblClick, {65, 30}, Qt::LeftButton, Qt::LeftButton);
    e.mouseEvent(QEvent::MouseButtonRelease, {65, 30}, Qt::LeftButton, Qt::NoButton);
    e.expectPrim(0, "LI 20 30 60 30 0");
    e.expectPrim(1, "LI 60 30 65 30 0");
    e.click({80, 50});
    CHECK(e.doc.count() == 2); // double-click finished the old chain
    e.key(Qt::Key_S);
    e.click({40, 30});
    CHECK(e.c->selection() == QList<int>{0});
    e.drag({60, 30}, {70, 40});
    e.expectPrim(0, "LI 20 30 70 40 0");
    e.key(Qt::Key_Z, Qt::ControlModifier);
    e.expectPrim(0, "LI 20 30 60 30 0");
    // Even without a move event, the release owns the final drag coordinate.
    e.drag({60, 30}, {75, 45}, false);
    e.expectPrim(0, "LI 20 30 75 45 0");
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    fc_set_argv0(argv[0]);
    test_final_pointer_position(app);
    printf("ui e2e: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
