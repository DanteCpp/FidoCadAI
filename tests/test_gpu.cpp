#include "../gui/canvas.h"
#include "../gui/props.h"
#include <QApplication>
#include <QBuffer>
#include <QLineEdit>
#include <QOpenGLWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <cstdio>

static int failed;
#define CHECK(c) do { if (!(c)) { ++failed; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir temp;
    Settings settings(temp.path() + "/gui.conf");
    settings.set("paper", "white");
    settings.set("show_grid", "false");
    Theme theme = Theme::load("light");
    Document doc;
    CHECK(doc.loadLibraries(true, {}, {}));
    CHECK(doc.openText("[FIDOCAD]\nRP 20 20 40 40 0\nEV 50 20 70 40 0\n"
                       "LI 10 70 90 70 0\nTY 20 50 4 4 0 0 0 * GPU $x^2$\n"));
    QImage bitmap(4, 4, QImage::Format_RGBA8888);
    bitmap.fill(QColor(255, 0, 0, 128));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    bitmap.save(&buffer, "PNG");
    CHECK(doc.addImage(bytes, {75, 75}, 0) == 4);
    Canvas canvas(&doc, &settings, &theme);
    doc.onChanged = [&] { canvas.docChanged(); };
    canvas.resize(640, 480);
    canvas.show();
    canvas.fitRect(QRectF(0, 0, 100, 100));
    QTest::qWait(200);
    const auto surfaces = canvas.findChildren<QOpenGLWidget *>();
    CHECK(surfaces.size() == 1);
    CHECK(!canvas.gpuRenderer().isEmpty());
    fprintf(stderr, "GPU renderer: %s\n", qPrintable(canvas.gpuRenderer()));
    if (surfaces.isEmpty()) return 1;
    QOpenGLWidget *surface = surfaces.first();
    CHECK(surface->isValid());
    auto shot = [&] { return surface->grabFramebuffer(); };
    auto sample = [&](const QImage &image, QPointF logical) {
        QPoint pos = (canvas.toPixel(logical) * surface->devicePixelRatioF()).toPoint();
        return image.pixelColor(pos);
    };
    const QImage initial = shot();
    CHECK(!initial.isNull());
    CHECK(sample(initial, {30, 30}).lightness() < 100);
    CHECK(sample(initial, {60, 30}).lightness() > 230); // hollow ellipse
    CHECK(sample(initial, {5, 5}).lightness() > 230);
    const QColor imagePixel = sample(initial, {85, 85});
    CHECK(imagePixel.red() > 250 && std::abs(imagePixel.green() - 127) <= 2);
    FcRect labelBounds;
    fc_label_bounds(&doc.drawing().prims[3].u.text, 1, &labelBounds);
    QRect textArea((canvas.toPixel({labelBounds.min_x, labelBounds.min_y}) * surface->devicePixelRatioF()).toPoint(),
                   (canvas.toPixel({labelBounds.max_x, labelBounds.max_y}) * surface->devicePixelRatioF()).toPoint());
    int textInk = 0;
    for (int y = textArea.top(); y <= textArea.bottom(); ++y)
        for (int x = textArea.left(); x <= textArea.right(); ++x)
            if (initial.pixelColor(x, y).lightness() < 150) ++textInk;
    CHECK(textInk > 30);
    // The whole viewport must remain correctly drawn immediately during a
    // zoom / large pan, without waiting for a cached raster to settle.
    const QPointF anchor = canvas.toPixel({30, 30});
    canvas.zoomBy(1.37, anchor);
    CHECK((canvas.toPixel({30, 30}) - anchor).manhattanLength() < 1e-6);
    QImage zoomed = shot();
    CHECK(sample(zoomed, {30, 30}).lightness() < 100);
    QPointF at = canvas.toPixel({30, 30});
    QWheelEvent pan(at, canvas.mapToGlobal(at.toPoint()), {80, -30}, {}, Qt::NoButton,
                    Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(&canvas, &pan);
    CHECK(canvas.toPixel({30, 30}) - at == QPointF(80, -30));
    CHECK(sample(shot(), {30, 30}).lightness() < 100);
    doc.remove({0});
    CHECK(sample(shot(), {30, 30}).lightness() > 230);
    // An inline editor must stay above the OpenGL child and retain focus.
    canvas.setSelection({2});
    canvas.editSelection();
    CHECK(canvas.editing());
    if (canvas.form()) {
        const auto fields = canvas.form()->findChildren<QLineEdit *>();
        CHECK(!fields.isEmpty());
        if (!fields.isEmpty()) {
            // QWidget keeps its intended focus child even on an inactive
            // workspace; querying QApplication focus would require stealing
            // the user's desktop focus to make this assertion meaningful.
            CHECK(canvas.focusWidget() == fields.first());
            CHECK(canvas.childAt(fields.first()->mapTo(&canvas, fields.first()->rect().center())) == fields.first());
        }
    }
    printf("GPU canvas: %d failures\n", failed);
    return failed ? 1 : 0;
}
