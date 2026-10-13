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
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cmath>
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
    Qt::MouseButtons buttons = Qt::NoButton;

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
        mouseEvent(QEvent::MouseButtonPress, logical, button, buttons | button, mods);
        mouseEvent(QEvent::MouseButtonRelease, logical, button, buttons & ~button, mods);
    }
    void move(QPointF logical) {
        mouseEvent(QEvent::MouseMove, logical, Qt::NoButton, buttons);
    }
    void clickWidget(QWidget *widget, QPoint at) {
        QPoint pos = widget->mapTo(&win, at);
        windowMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
        windowMouse(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
    }
    void type(const QString &text) {
        CHECK(QApplication::focusWidget() != nullptr);
        for (char ch : text.toLatin1()) QTest::keyClick(win.windowHandle(), ch);
        QApplication::processEvents();
    }
    void doublePress(QPointF logical, Qt::KeyboardModifiers mods = {}) {
        mouseEvent(QEvent::MouseButtonDblClick, logical, Qt::LeftButton, Qt::LeftButton, mods);
        mouseEvent(QEvent::MouseButtonRelease, logical, Qt::LeftButton, Qt::NoButton, mods);
    }
    void symbol(const QString &name = "080") {
        key(Qt::Key_B, Qt::ControlModifier);
        CHECK(settings.getBool("palette"));
        QLineEdit *search = nullptr;
        for (auto *edit : win.findChildren<QLineEdit *>())
            if (edit->placeholderText() == "Search symbols…") search = edit;
        CHECK(search != nullptr);
        if (!search) return;
        clickWidget(search, search->rect().center());
        key(Qt::Key_A, Qt::ControlModifier);
        type(name);
        auto *tree = win.findChild<QTreeWidget *>();
        QTreeWidgetItem *item = nullptr;
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString() == name) item = *it;
        CHECK(item != nullptr);
        if (item) {
            CHECK(!tree->visualItemRect(item).isEmpty());
            clickWidget(tree->viewport(), tree->visualItemRect(item).center());
        }
        CHECK(c->tool() == Canvas::Macro);
        key(Qt::Key_B, Qt::ControlModifier);
        CHECK(!settings.getBool("palette"));
    }
    void wheel(QPointF at, QPoint pixels, QPoint angles, Qt::KeyboardModifiers mods = {}) {
        QPoint pos = point(at);
        QWheelEvent event(pos, win.mapToGlobal(pos), pixels, angles, Qt::NoButton,
                          mods, Qt::ScrollUpdate, false);
        QApplication::sendEvent(win.windowHandle(), &event);
        QApplication::processEvents();
    }
    void mouseEvent(QEvent::Type type, QPointF logical, Qt::MouseButton button,
                    Qt::MouseButtons held, Qt::KeyboardModifiers mods = {}) {
        windowMouse(type, point(logical), button, held, mods);
    }
    void windowMouse(QEvent::Type type, QPoint pos, Qt::MouseButton button,
                     Qt::MouseButtons held, Qt::KeyboardModifiers mods = {}) {
        buttons = held;
        QMouseEvent event(type, pos, win.mapToGlobal(pos), button, held, mods);
        QApplication::sendEvent(win.windowHandle(), &event);
        QApplication::processEvents();
    }
    void drag(QPointF from, QPointF to, bool intermediate = true,
              Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers mods = {}) {
        // Resolve all screen positions before a pan changes the transform.
        const QPoint start = point(from), middle = point((from + to) / 2), end = point(to);
        windowMouse(QEvent::MouseButtonPress, start, button, button, mods);
        if (intermediate) windowMouse(QEvent::MouseMove, middle, Qt::NoButton, button, mods);
        // QTest::mouseRelease(QWindow*) inserts an extra move to the release
        // position. Real input may coalesce moves; send that release itself.
        windowMouse(QEvent::MouseButtonRelease, end, button, Qt::NoButton, mods);
        QApplication::processEvents();
    }
    void expectPrim(int i, const QString &expected) {
        if (doc.primFcd(i) != expected)
            fprintf(stderr, "expected [%s], got [%s]\n", qPrintable(expected), qPrintable(doc.primFcd(i)));
        CHECK(doc.primFcd(i) == expected);
    }
};

static void test_tools(QApplication &app) {
    struct ToolCase { Qt::Key key; Canvas::Tool tool; const char *code; bool filled; };
    const ToolCase cases[] = {
        {Qt::Key_L, Canvas::Line, "LI", false},
        {Qt::Key_B, Canvas::Rect, "RV", false},
        {Qt::Key_B, Canvas::Rect, "RP", true},
        {Qt::Key_E, Canvas::Oval, "EV", false},
        {Qt::Key_E, Canvas::Oval, "EP", true},
        {Qt::Key_J, Canvas::Dot, "SA", false},
        {Qt::Key_T, Canvas::Text, "TY", false},
        {Qt::Key_unknown, Canvas::Macro, "MC", false},
    };
    for (int view = 0; view < 3; ++view) {
        for (const auto &t : cases) {
            Editor e(app);
            if (t.tool == Canvas::Macro) e.symbol();
            else e.key(t.key);
            CHECK(e.c->tool() == t.tool);
            if (view > 0) {
                e.key(Qt::Key_Plus);
                e.drag({50, 50}, {56, 53}, true, Qt::MiddleButton);
            }
            const bool snap = view != 2;
            if (!snap) e.key(Qt::Key_G, Qt::ShiftModifier);
            CHECK(e.settings.getBool("snap") == snap);
            if (t.filled) e.key(Qt::Key_F);
            CHECK(e.settings.getBool("filled") == t.filled);
            e.key(Qt::Key_BracketRight);
            const QPointF a(33.2, 44.2), b(82.2, 73.2);
            const QString start = snap ? "35 45" : "33 44";
            const QString end = snap ? "80 75" : "82 73";
            e.move({10, 10}); // stale preview must not determine the click
            e.click(a);
            if (t.tool == Canvas::Text) {
                CHECK(e.c->editing());
                e.type("First label");
                e.key(Qt::Key_Return);
            }
            e.doublePress(b);
            if (t.tool == Canvas::Text) {
                CHECK(e.c->editing());
                e.type("Second label");
                e.key(Qt::Key_Return);
            }
            if (t.tool == Canvas::Line || t.tool == Canvas::Rect || t.tool == Canvas::Oval) {
                CHECK(e.doc.count() == 1);
                e.expectPrim(0, QString("%1 %2 %3 1").arg(t.code, start, end));
            } else {
                CHECK(e.doc.count() == 2);
                for (int i = 0; i < 2; ++i) {
                    const QString pos = i ? end : start;
                    if (t.tool == Canvas::Dot) e.expectPrim(i, "SA " + pos + " 1");
                    if (t.tool == Canvas::Macro) e.expectPrim(i, "MC " + pos + " 0 0 080");
                    if (t.tool == Canvas::Text)
                        e.expectPrim(i, "TY " + pos + " 4 3 0 0 1 * " + (i ? "Second label" : "First label"));
                }
            }
            e.key(Qt::Key_Escape);
            CHECK(e.c->tool() == Canvas::Select);
        }
    }
    for (Qt::Key tool : {Qt::Key_B, Qt::Key_E}) {
        for (bool filled : {false, true}) {
            Editor e(app);
            e.key(tool);
            if (filled) e.key(Qt::Key_F);
            e.drag({30, 40}, {80, 75});
            e.expectPrim(0, QString("%1 30 40 80 75 0")
                              .arg(tool == Qt::Key_B ? (filled ? "RP" : "RV") : (filled ? "EP" : "EV")));
            // A second corner can also be a plain Shift-click.
            e.click({100, 40});
            e.click({130, 75}, Qt::LeftButton, Qt::ShiftModifier);
            CHECK(e.doc.count() == 2);
            CHECK(e.doc.handle(1, 1) == QPointF(130, 75));
        }
    }
}

static void test_line_completion(QApplication &app) {
    for (int finish = 0; finish < 5; ++finish) {
        Editor e(app);
        e.key(Qt::Key_L);
        e.click({20, 30});
        e.click({60, 40}, Qt::LeftButton, Qt::ShiftModifier);
        e.click({70, 70}, Qt::LeftButton, Qt::ShiftModifier);
        e.expectPrim(0, "LI 20 30 60 30 0");
        e.expectPrim(1, "LI 60 30 60 70 0");
        switch (finish) {
        case 0: e.key(Qt::Key_Escape); break;
        case 1: e.key(Qt::Key_Return); break;
        case 2: e.key(Qt::Key_Enter); break;
        case 3: e.click({60, 70}, Qt::RightButton); break;
        case 4: e.doublePress({60, 70}); break;
        }
        CHECK(e.doc.count() == 2); // never append a zero-length segment
        CHECK(e.c->tool() == Canvas::Line);
        e.click({100, 50});
        CHECK(e.doc.count() == 2); // a fresh chain
        e.click({130, 50});
        e.expectPrim(2, "LI 100 50 130 50 0");
        e.key(Qt::Key_Escape);
        e.key(Qt::Key_Escape);
        CHECK(e.c->tool() == Canvas::Select);
    }
}

static void test_primitive_drags(QApplication &app) {
    // Every FidoCad primitive, including those edited via handles rather than
    // created by a dedicated tool. A frame keeps the viewport consistent.
    const char *primitives[] = {
        "LI 40 40 80 40 0", "BE 40 40 50 60 70 60 80 40 0",
        "RV 40 40 80 70 0", "RP 40 40 80 70 0", "EV 40 40 80 70 0", "EP 40 40 80 70 0",
        "PV 40 40 80 40 60 70 0", "PP 40 40 80 40 60 70 0",
        "CV 0 40 40 50 60 70 60 80 40 0", "CP 1 40 40 50 60 70 60 80 40 0",
        "SA 40 40 0", "PL 40 40 80 40 3 1", "PA 40 40 8 6 2 0 1",
        "TY 40 40 4 3 0 0 0 * Label", "MC 40 40 0 0 080",
    };
    for (const char *prim : primitives) {
        Editor e(app, QByteArray("[FIDOCAD]\n") + prim + "\nRV 0 0 200 150 15\n");
        e.key(Qt::Key_S);
        e.key(Qt::Key_A, Qt::ControlModifier);
        CHECK(e.c->selection().size() == 2);
        const QByteArray before = e.doc.fcd();
        CHECK(e.doc.handle(0, 0) == QPointF(40, 40));
        e.drag({40, 40}, {55, 55});
        CHECK(e.doc.handle(0, 0) == QPointF(55, 55));
        e.expectPrim(1, "RV 0 0 200 150 15");
        const QByteArray after = e.doc.fcd();
        e.key(Qt::Key_Z, Qt::ControlModifier);
        CHECK(e.doc.fcd() == before);
        e.key(Qt::Key_Y, Qt::ControlModifier);
        CHECK(e.doc.fcd() == after);
        // A release with no preceding movement must work for every kind too.
        e.drag({55, 55}, {60, 65}, false);
        CHECK(e.doc.handle(0, 0) == QPointF(60, 65));
    }
    // Body dragging uses the final release delta for components and shapes.
    for (const char *prim : {"LI 40 40 80 40 0", "MC 40 40 0 0 080"}) {
        Editor e(app, QByteArray(prim) + "\nRV 0 0 200 150 15\n");
        const QPointF hit = e.doc.drawing().prims[0].kind == FC_LINE ? QPointF(60, 40) : QPointF(45, 40);
        e.drag(hit, hit + QPointF(20, 15));
        CHECK(e.c->selection() == QList<int>{0});
        CHECK(e.doc.handle(0, 0) == QPointF(60, 55));
    }
}

static void test_edit_shortcuts(QApplication &app) {
    Editor e(app);
    e.key(Qt::Key_B);
    e.click({30, 30});
    e.click({70, 60});
    e.key(Qt::Key_S);
    CHECK(e.c->tool() == Canvas::Select);
    e.click({50, 30});
    CHECK(e.c->selection() == QList<int>{0});
    e.key(Qt::Key_Right);
    e.expectPrim(0, "RV 35 30 75 60 0");
    e.key(Qt::Key_Down);
    e.expectPrim(0, "RV 35 35 75 65 0");
    e.key(Qt::Key_Left);
    e.expectPrim(0, "RV 30 35 70 65 0");
    e.key(Qt::Key_Up);
    e.expectPrim(0, "RV 30 30 70 60 0");
    e.key(Qt::Key_BracketLeft);
    e.expectPrim(0, "RV 30 30 70 60 15");
    CHECK(e.settings.getInt("layer") == 15);
    e.key(Qt::Key_BracketRight);
    e.expectPrim(0, "RV 30 30 70 60 0");
    CHECK(e.settings.getInt("layer") == 0);

    e.key(Qt::Key_C, Qt::ControlModifier);
    CHECK(QApplication::clipboard()->text().contains("RV 30 30 70 60 0"));
    e.key(Qt::Key_D, Qt::ControlModifier);
    e.expectPrim(1, "RV 35 35 75 65 0");
    CHECK(e.c->selection() == QList<int>{1});
    e.key(Qt::Key_X, Qt::ControlModifier);
    CHECK(e.doc.count() == 1);
    CHECK(QApplication::clipboard()->text().contains("RV 35 35 75 65 0"));
    e.move({100, 80});
    e.key(Qt::Key_V, Qt::ControlModifier);
    CHECK(e.doc.count() == 2);
    e.expectPrim(1, "RV 100 80 140 110 0");
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(e.doc.count() == 1);
    e.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    e.expectPrim(1, "RV 100 80 140 110 0");
    e.key(Qt::Key_Z, Qt::ControlModifier);
    e.key(Qt::Key_Y, Qt::ControlModifier);
    CHECK(e.doc.count() == 2);
    for (Qt::Key del : {Qt::Key_Delete, Qt::Key_Backspace}) {
        e.key(Qt::Key_A, Qt::ControlModifier);
        CHECK(e.c->selection().size() == 2);
        e.key(del);
        CHECK(e.doc.count() == 0);
        e.key(Qt::Key_Z, Qt::ControlModifier);
        CHECK(e.doc.count() == 2);
    }
    e.key(Qt::Key_A, Qt::ControlModifier);
    e.key(Qt::Key_Escape);
    CHECK(e.c->selection().isEmpty());

    // Selection by box, additive Ctrl-click, and Escape use pointer hit tests.
    e.drag({25, 25}, {75, 65});
    CHECK(e.c->selection() == QList<int>{0});
    e.click({120, 80}, Qt::LeftButton, Qt::ControlModifier);
    CHECK(e.c->selection().size() == 2);
    e.click({50, 30}, Qt::LeftButton, Qt::ControlModifier);
    CHECK(e.c->selection() == QList<int>{1});
    e.key(Qt::Key_Escape);
    CHECK(e.c->selection().isEmpty());

    // All three properties gestures route to the form. Text input must not
    // activate canvas shortcuts; Escape cancels, Return applies.
    e.click({50, 30});
    for (Qt::Key properties : {Qt::Key_P, Qt::Key_F2}) {
        const QByteArray before = e.doc.fcd();
        e.key(properties);
        CHECK(e.c->editing());
        e.type("slbejtkrmfg");
        CHECK(e.c->tool() == Canvas::Select);
        e.key(Qt::Key_Escape);
        CHECK(!e.c->editing() && e.doc.fcd() == before);
    }
    e.doublePress({50, 30});
    CHECK(e.c->editing());
    e.type("Box1");
    e.key(Qt::Key_Tab);
    e.type("Value1");
    e.key(Qt::Key_Return);
    CHECK(!e.c->editing());
    CHECK(e.doc.primFcd(0).contains("Box1") && e.doc.primFcd(0).contains("Value1"));
    e.key(Qt::Key_S, Qt::ControlModifier);
    CHECK(!e.doc.modified());
    QFile saved(e.doc.path());
    CHECK(saved.open(QIODevice::ReadOnly));
    CHECK(saved.readAll() == e.doc.fcd());
    e.key(Qt::Key_W, Qt::ControlModifier);
    CHECK(!e.win.isVisible());
}

static void test_component_transform_shortcuts(QApplication &app) {
    Editor e(app);
    e.symbol();
    for (int mirror = 0; mirror < 2; ++mirror) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            const int i = mirror * 4 + rotation;
            e.click({30.0 + rotation * 30, 40.0 + mirror * 40});
            e.expectPrim(i, QString("MC %1 %2 %3 %4 080")
                              .arg(30 + rotation * 30).arg(40 + mirror * 40).arg(rotation).arg(mirror));
            e.key(Qt::Key_R);
        }
        e.key(Qt::Key_M);
    }
    e.click({180, 120}, Qt::RightButton);
    CHECK(e.c->tool() == Canvas::Select);
    e.click({35, 40});
    CHECK(e.c->selection() == QList<int>{0});
    e.key(Qt::Key_R);
    e.expectPrim(0, "MC 30 40 1 0 080");
    e.key(Qt::Key_M);
    e.expectPrim(0, "MC 30 40 1 1 080");
    e.key(Qt::Key_Z, Qt::ControlModifier);
    e.expectPrim(0, "MC 30 40 1 0 080");
    e.key(Qt::Key_Return, Qt::ControlModifier);
    CHECK(e.win.saved() && e.win.status() == "done" && !e.win.isVisible());
    QFile saved(e.doc.path());
    CHECK(saved.open(QIODevice::ReadOnly));
    CHECK(saved.readAll() == e.doc.fcd());
}

static void test_view_and_ruler_shortcuts(QApplication &app) {
    Editor e(app);
    auto scale = [&] { return e.c->toPixel({1, 0}).x() - e.c->toPixel({0, 0}).x(); };
    const double initial = scale();
    for (Qt::Key plus : {Qt::Key_Plus, Qt::Key_Equal}) {
        e.key(plus);
        CHECK(std::abs(scale() / initial - 1.25) < 1e-6);
        e.key(Qt::Key_Minus);
        CHECK(std::abs(scale() - initial) < 1e-6);
    }
    QPointF origin = e.c->toPixel({0, 0});
    const QPointF a(50, 50), b(60, 55);
    QPoint delta = e.point(b) - e.point(a);
    e.drag(a, b, true, Qt::MiddleButton);
    CHECK((e.c->toPixel({0, 0}) - origin - delta).manhattanLength() < 1e-6);
    for (Qt::Key fit : {Qt::Key_0, Qt::Key_Space}) {
        e.key(Qt::Key_Equal);
        e.key(fit);
        CHECK(std::abs(scale() - initial) < 1e-6);
        CHECK((e.c->toPixel({0, 0}) - origin).manhattanLength() < 1e-6);
    }
    const QPointF anchor(60, 60), pixel = e.c->toPixel(anchor);
    e.wheel(anchor, {}, {0, 120});
    CHECK(std::abs(scale() / initial - 1.2) < 1e-6);
    CHECK((e.c->toPixel(anchor) - pixel).manhattanLength() < 1);
    origin = e.c->toPixel({0, 0});
    e.wheel(anchor, {12, -7}, {});
    CHECK(e.c->toPixel({0, 0}) - origin == QPointF(12, -7));
    const double beforeCtrlScroll = scale();
    e.wheel(anchor, {0, 10}, {0, 120}, Qt::ControlModifier);
    CHECK(std::abs(scale() / beforeCtrlScroll - 1.2) < 1e-6);
    e.key(Qt::Key_Space);
    for (Qt::Key tool : {Qt::Key_S, Qt::Key_L, Qt::Key_B, Qt::Key_E, Qt::Key_J, Qt::Key_T}) {
        e.key(tool);
        for (bool shift : {false, true}) {
            e.drag({30, 40}, {60, 80}, false, shift ? Qt::LeftButton : Qt::RightButton,
                   shift ? Qt::ShiftModifier : Qt::NoModifier);
            CHECK(e.c->rulerVisible());
            CHECK(std::abs(e.c->rulerLength() - 50) < 0.5);
            CHECK(std::abs(e.c->rulerAngle() + 53.1301) < 0.5);
            CHECK(e.doc.count() == 0);
            e.key(Qt::Key_Escape);
            CHECK(!e.c->rulerVisible());
        }
    }
    e.key(Qt::Key_Escape);
    CHECK(e.c->tool() == Canvas::Select);
    bool grid = e.settings.getBool("show_grid");
    e.key(Qt::Key_G);
    CHECK(e.settings.getBool("show_grid") != grid);
    e.key(Qt::Key_G);
    CHECK(e.settings.getBool("show_grid") == grid);
    CHECK(!e.win.barVisible());
    e.key(Qt::Key_I);
    CHECK(e.win.barVisible());
    e.key(Qt::Key_I);
    CHECK(!e.win.barVisible());
}

static void test_key_sheet(QApplication &app) {
    Editor e(app);
    e.key(Qt::Key_K);
    KeySheet *sheet = e.c->keySheet();
    CHECK(sheet->isVisible());
    e.type("grid");
    CHECK(sheet->query() == "grid");
    e.key(Qt::Key_Backspace);
    CHECK(sheet->query() == "gri");
    e.key(Qt::Key_Escape);
    CHECK(!sheet->isVisible());
    e.key(Qt::Key_K);
    CHECK(sheet->query().isEmpty());
    for (Qt::Key down : {Qt::Key_Down, Qt::Key_PageDown, Qt::Key_End}) {
        e.key(Qt::Key_Home);
        sheet->repaint();
        e.key(down);
        sheet->repaint();
        CHECK(sheet->scroll() > 0);
    }
    auto *bar = sheet->findChild<QScrollBar *>();
    CHECK(bar && bar->isVisible());
    int before = sheet->scroll();
    e.key(Qt::Key_Up);
    CHECK(sheet->scroll() < before);
    before = sheet->scroll();
    e.key(Qt::Key_PageUp);
    CHECK(sheet->scroll() < before);
    e.key(Qt::Key_Home);
    CHECK(sheet->scroll() == 0);
    e.clickWidget(sheet, sheet->rect().center());
    CHECK(sheet->isVisible() && e.doc.count() == 0);
    e.key(Qt::Key_Return);
    CHECK(!sheet->isVisible());
    e.key(Qt::Key_K);
    e.key(Qt::Key_Enter);
    CHECK(!sheet->isVisible());
    e.key(Qt::Key_K);
    e.key(Qt::Key_K);
    CHECK(!sheet->isVisible());
    e.key(Qt::Key_K);
    e.clickWidget(sheet, {1, 1});
    CHECK(!sheet->isVisible());
}

static void test_label_handle_gestures(QApplication &app) {
    Editor e(app, "MC 60 100 0 0 080\nFCJ\nTY 80 50 4 3 0 0 0 * R1\n"
                  "TY 80 65 4 3 0 0 0 * 10k\nMC 140 100 1 1 080\nFCJ\n"
                  "TY 160 50 4 3 0 0 0 * R2\nTY 160 65 4 3 0 0 0 * 20k\n"
                  "RV 0 0 200 150 15\n");
    const QByteArray before = e.doc.fcd();
    e.drag({75, 45}, {185, 75});
    CHECK((e.c->selection() == QList<int>{0, 1}));
    e.drag({80, 50}, {90, 55});
    CHECK(e.doc.handle(0, 1) == QPointF(90, 55));
    CHECK(e.doc.handle(0, 2) == QPointF(90, 70));
    CHECK(e.doc.handle(1, 1) == QPointF(170, 55));
    CHECK(e.doc.handle(1, 2) == QPointF(170, 70));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(e.doc.fcd() == before);
    e.key(Qt::Key_A, Qt::ControlModifier);
    e.click({80, 50}, Qt::LeftButton, Qt::ControlModifier);
    e.click({80, 65}, Qt::LeftButton, Qt::ControlModifier);
    e.click({160, 50}, Qt::LeftButton, Qt::ControlModifier);
    e.click({160, 50}, Qt::LeftButton, Qt::ControlModifier); // toggle off again
    e.drag({80, 65}, {90, 75});
    CHECK(e.doc.handle(0, 1) == QPointF(90, 60));
    CHECK(e.doc.handle(0, 2) == QPointF(90, 75));
    CHECK(e.doc.handle(1, 1) == QPointF(160, 50));
    e.drag({155, 45}, {185, 75}, true, Qt::LeftButton, Qt::ControlModifier);
    e.key(Qt::Key_Right);
    CHECK(e.doc.handle(0, 1) == QPointF(95, 60));
    CHECK(e.doc.handle(0, 2) == QPointF(95, 75));
    CHECK(e.doc.handle(1, 1) == QPointF(165, 50));
    CHECK(e.doc.handle(1, 2) == QPointF(165, 65));
    CHECK(e.doc.primFcd(0).startsWith("MC 60 100 0 0 080"));
    CHECK(e.doc.primFcd(1).startsWith("MC 140 100 1 1 080"));
}

static void test_native_gestures(QApplication &app) {
    Editor e(app);
    // Native gesture routing uses screen hit testing. Keep the gesture point
    // inside the offscreen platform's small logical screen at double scaling.
    e.win.resize(640, 420);
    QApplication::processEvents();
    auto gesture = [&](Qt::NativeGestureType type, double value, QPointF delta = {}) {
        QPoint pos = e.point({100, 75});
        QNativeGestureEvent event(type, QPointingDevice::primaryPointingDevice(), 2,
                                  pos, pos, e.win.mapToGlobal(pos), value, delta);
        QApplication::sendEvent(e.win.windowHandle(), &event);
    };
    const QPointF origin = e.c->toPixel({0, 0});
    const QPointF anchor = e.c->toPixel({100, 75});
    const double scale = e.c->toPixel({1, 0}).x() - origin.x();
    gesture(Qt::BeginNativeGesture, 0);
    gesture(Qt::ZoomNativeGesture, 0.5);
    gesture(Qt::EndNativeGesture, 0);
    CHECK(std::abs((e.c->toPixel({1, 0}).x() - e.c->toPixel({0, 0}).x()) / scale - 1.5) < 1e-6);
    CHECK((e.c->toPixel({100, 75}) - anchor).manhattanLength() < 1);
    const QPointF zoomed = e.c->toPixel({0, 0});
    gesture(Qt::PanNativeGesture, 0, {12, -7});
    CHECK(e.c->toPixel({0, 0}) - zoomed == QPointF(12, -7));
    gesture(Qt::SmartZoomNativeGesture, 0);
    CHECK((e.c->toPixel({0, 0}) - origin).manhattanLength() < 1e-6);
}

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
    test_tools(app);
    test_line_completion(app);
    test_primitive_drags(app);
    test_edit_shortcuts(app);
    test_component_transform_shortcuts(app);
    test_view_and_ruler_shortcuts(app);
    test_key_sheet(app);
    test_label_handle_gestures(app);
    test_native_gestures(app);
    printf("ui e2e: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
