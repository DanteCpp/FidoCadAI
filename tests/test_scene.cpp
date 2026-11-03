#include "../gui/scene.h"
#include <QApplication>
#include <QBuffer>
#include <QPainter>
#include <cstdio>

static int failed;
#define CHECK(c) do { if (!(c)) { ++failed; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const char *cases[] = {
        "LI 10 10 60 40 0\nFCJ 3 0 3 1 0 0.5 0\n",
        "RV 10 10 60 40 0\n", "RP 10 10 60 40 0\n",
        "EV 10 10 60 40 0\n", "EP 10 10 60 40 0\n",
        "PV 10 10 60 10 35 40 0\n", "PP 10 10 60 10 35 40 0\n",
        "CV 0 10 20 30 40 50 20 0\n", "CP 1 10 20 30 40 50 20 0\n",
        "BE 10 20 25 0 40 50 60 20 0\nFCJ 3 0 3 1 0 0.5 0\n",
        "PL 10 10 60 40 3 0\n", "PA 35 25 12 8 3 0 0\n",
        "PA 35 25 12 8 3 1 0\n", "SA 35 25 0\n",
        "TY 10 20 6 4 0 0 0 * Label $x^2$\n",
        "TY 40 45 6 4 90 7 0 * Label\n",
        "MC 35 25 0 0 unknown\n"
    };
    for (const char *text : cases) {
        FcDrawing drawing;
        fc_drawing_init(&drawing);
        CHECK(fc_parse(&drawing, text, 0) == FC_OK);
        std::array<QColor, FC_MAX_LAYERS> colors;
        colors.fill(Qt::black);
        uint32_t ink[FC_MAX_LAYERS]{};
        Scene scene;
        scene.rebuild(drawing, nullptr, colors, true, 4);
        QImage vector(320, 240, QImage::Format_RGB32);
        vector.fill(Qt::white);
        {
            QPainter painter(&vector);
            painter.translate(20, 20);
            painter.scale(4, 4);
            scene.paint(painter, QRectF(-5, -5, 80, 60));
        }
        QImage raster(320, 240, QImage::Format_RGB888);
        FcView view{};
        view.ox = view.oy = 20;
        view.scale = 4;
        view.bg = 0xffffff;
        view.layer_rgb = ink;
        view.tex = true;
        CHECK(fc_render_rgb(&drawing, nullptr, &view, raster.bits(), 320, 240,
                            int(raster.bytesPerLine())) == FC_OK);
        // Different rasterizers have different edge coverage. Require ink to
        // agree spatially within two pixels in both directions, rather than
        // demanding byte-identical antialiasing.
        auto mismatch = [](const QImage &a, const QImage &b) {
            int inkCount = 0, missing = 0;
            for (int y = 2; y < a.height() - 2; ++y) for (int x = 2; x < a.width() - 2; ++x) {
                if (a.pixelColor(x, y).lightness() >= 180) continue;
                ++inkCount;
                bool found = false;
                for (int dy = -2; dy <= 2 && !found; ++dy)
                    for (int dx = -2; dx <= 2 && !found; ++dx)
                        found = b.pixelColor(x + dx, y + dy).lightness() < 180;
                if (!found) ++missing;
            }
            return inkCount ? double(missing) / inkCount : 1.0;
        };
        double forward = mismatch(vector, raster), reverse = mismatch(raster, vector);
        if (forward > .04 || reverse > .04)
            fprintf(stderr, "Scene mismatch: %s %.3f / %.3f\n", text, forward, reverse);
        CHECK(forward <= .04 && reverse <= .04);
        fc_drawing_free(&drawing);
    }
    // RGBA image textures must preserve alpha, rotation and mirroring.
    FcDrawing drawing;
    fc_drawing_init(&drawing);
    QImage bitmap(4, 4, QImage::Format_RGBA8888);
    bitmap.fill(QColor(255, 0, 0, 128));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    bitmap.save(&buffer, "PNG");
    CHECK(fc_drawing_add_image(&drawing, bytes.constData(), size_t(bytes.size()), 50, 50, 20, 10, 0) == 0);
    drawing.prims[0].u.image.rotation = 90;
    drawing.prims[0].u.image.mirror = 1;
    std::array<QColor, FC_MAX_LAYERS> colors;
    colors.fill(Qt::black);
    Scene scene;
    scene.rebuild(drawing, nullptr, colors, true, 1);
    fc_drawing_free(&drawing); // retained images must own their pixels
    QImage output(100, 100, QImage::Format_RGB32);
    output.fill(Qt::white);
    {
        QPainter p(&output);
        scene.paint(p, QRectF(0, 0, 100, 100));
    }
    QColor pixel = output.pixelColor(45, 40);
    CHECK(pixel.red() == 255 && std::abs(pixel.green() - 127) <= 1 && pixel.blue() == pixel.green());
    CHECK(output.pixelColor(55, 60) == QColor(Qt::white));
    printf("Retained scene: %d failures\n", failed);
    return failed ? 1 : 0;
}
