#include "gui/canvas.h"
#include "gui/document.h"
#include "gui/settings.h"
#include "gui/theme.h"
#include "gui/window.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QTest>
#include <cstdio>
#include <vector>
#include <string>

static void drawMacWindow(QPainter &p, const QRect &r, const QString &title, bool dark) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    // Window shadow / border
    QColor bg = dark ? QColor(22, 24, 29) : QColor(245, 246, 248);
    QColor border = dark ? QColor(45, 48, 56) : QColor(215, 218, 224);
    QColor titleBarBg = dark ? QColor(28, 30, 36) : QColor(235, 237, 240);
    QColor titleColor = dark ? QColor(160, 165, 175) : QColor(100, 105, 115);

    p.setPen(border);
    p.setBrush(bg);
    p.drawRoundedRect(r, 10, 10);

    // Titlebar
    QRect tb(r.left(), r.top(), r.width(), 34);
    p.save();
    p.setClipRect(tb);
    p.setPen(border);
    p.setBrush(titleBarBg);
    p.drawRoundedRect(r, 10, 10);
    p.restore();

    // Bottom border of titlebar
    p.setPen(border);
    p.drawLine(r.left(), r.top() + 34, r.right(), r.top() + 34);

    // macOS traffic lights
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 95, 86));
    p.drawEllipse(r.left() + 14, r.top() + 11, 12, 12);
    p.setBrush(QColor(255, 189, 46));
    p.drawEllipse(r.left() + 34, r.top() + 11, 12, 12);
    p.setBrush(QColor(39, 201, 63));
    p.drawEllipse(r.left() + 54, r.top() + 11, 12, 12);

    // Title
    QFont font = p.font();
    font.setPointSize(11);
    font.setBold(true);
    p.setFont(font);
    p.setPen(titleColor);
    p.drawText(tb, Qt::AlignCenter, title);

    p.restore();
}

struct StepInfo {
    QString fcd;
    std::vector<QString> terminalLines;
    QList<int> selection;
    QString bottomBarNote;
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QDir().mkpath("docs/images");
    QDir().mkpath("/tmp/fcai_frames");

    printf("Generating standalone screenshots...\n");

    // 1. PMOS diff-pair in dark mode
    {
        Settings settings("");
        settings.set("statusbar", "1");
        Theme theme = Theme::load("dark");
        Document doc;
        doc.loadLibraries(true, {}, {}, argv[0]);
        QString err;
        if (doc.open("playground/diff-pair/pmos_diffpair_130nm.fcd", &err)) {
            MainWindow win(&doc, &settings, &theme, &app, "");
            win.resize(1100, 750);
            win.show();
            QTest::qWait(250);
            win.grab().save("docs/images/circuit_diffpair.png");
            printf("Saved circuit_diffpair.png\n");
        }
    }

    // 2. Buck-boost schematic in dark mode
    {
        Settings settings("");
        settings.set("statusbar", "1");
        Theme theme = Theme::load("dark");
        Document doc;
        doc.loadLibraries(true, {}, {}, argv[0]);
        QString err;
        if (doc.open("playground/buck-boost/buck_boost.fcd", &err)) {
            MainWindow win(&doc, &settings, &theme, &app, "");
            win.resize(1100, 600);
            win.show();
            QTest::qWait(250);
            win.grab().save("docs/images/circuit_buck_boost.png");
            printf("Saved circuit_buck_boost.png\n");
        }
    }

    // 3. Buck-boost PCB layout in dark mode
    {
        Settings settings("");
        settings.set("statusbar", "1");
        Theme theme = Theme::load("dark");
        Document doc;
        doc.loadLibraries(true, {}, {}, argv[0]);
        QString err;
        if (doc.open("playground/buck-boost/buck_boost_pcb.fcd", &err)) {
            MainWindow win(&doc, &settings, &theme, &app, "");
            win.resize(1100, 600);
            win.show();
            QTest::qWait(250);
            win.grab().save("docs/images/circuit_buck_boost_pcb.png");
            printf("Saved circuit_buck_boost_pcb.png\n");
        }
    }

    // 4. VCO-based ADC in dark mode
    {
        Settings settings("");
        settings.set("statusbar", "1");
        Theme theme = Theme::load("dark");
        Document doc;
        doc.loadLibraries(true, {}, {}, argv[0]);
        QString err;
        if (doc.open("playground/vco-adc/vco_adc.fcd", &err)) {
            MainWindow win(&doc, &settings, &theme, &app, "");
            win.resize(1100, 850);
            win.show();
            QTest::qWait(250);
            win.grab().save("docs/images/circuit_vco_adc.png");
            printf("Saved circuit_vco_adc.png\n");
        }
    }

    // 5. Cap-ADC in dark mode
    {
        Settings settings("");
        settings.set("statusbar", "1");
        Theme theme = Theme::load("dark");
        Document doc;
        doc.loadLibraries(true, {}, {}, argv[0]);
        QString err;
        if (doc.open("playground/cap-adc/cap_adc.fcd", &err)) {
            MainWindow win(&doc, &settings, &theme, &app, "");
            win.resize(1150, 650);
            win.show();
            QTest::qWait(250);
            win.grab().save("docs/images/circuit_cap_adc.png");
            printf("Saved circuit_cap_adc.png\n");
        }
    }

    // 6. GUI Editor with Symbol Browser (Palette)
    {
        Settings settings("");
        settings.set("palette", "1");
        settings.set("statusbar", "1");
        Theme theme = Theme::load("dark");
        Document doc;
        doc.loadLibraries(true, {}, {}, argv[0]);
        QString err;
        if (doc.open("playground/buck-boost/buck_boost.fcd", &err)) {
            MainWindow win(&doc, &settings, &theme, &app, "");
            win.resize(1200, 700);
            win.show();
            QTest::qWait(250);
            win.grab().save("docs/images/fcai_gui_palette.png");
            printf("Saved fcai_gui_palette.png\n");
        }
    }

    // 7. Light mode editor with selection handles
    {
        Settings settings("");
        settings.set("statusbar", "1");
        settings.set("paper", "white");
        settings.set("theme", "light");
        Theme theme = Theme::load("light");
        Document doc;
        doc.loadLibraries(true, {}, {}, argv[0]);
        QString err;
        if (doc.open("playground/buck-boost/buck_boost.fcd", &err)) {
            MainWindow win(&doc, &settings, &theme, &app, "");
            win.resize(1100, 600);
            win.canvas()->setSelection({18, 21}); // select Q1 and L1
            win.show();
            QTest::qWait(250);
            win.grab().save("docs/images/fcai_gui_light.png");
            printf("Saved fcai_gui_light.png\n");
        }
    }

    // -------------------------------------------------------------
    // 8. Generate Side-by-Side Animation: "Session with Pi"
    // -------------------------------------------------------------
    printf("Generating animation frames for Session with Pi...\n");

    std::vector<StepInfo> steps;

    // Step 0: User launches session
    steps.push_back({
        "[FIDOCAD]\n",
        {
            "$ pi \"draw an inverting buck-boost converter\"",
            "╭─ pi ──────────────────────────────────────────╮",
            "│ Opening live GUI session:                     │",
            "│   fcai gui buck_boost.fcd --session-dir /tmp/ │",
            "│ Waiting for window... [connected]             │",
            "╰───────────────────────────────────────────────╯"
        },
        {},
        "live session active · /tmp/bb.session"
    });

    // Step 1: Placing VIN, GND, CIN
    steps.push_back({
        "[FIDOCAD]\n"
        "TY 20 10 8 6 0 0 11 * Buck-boost converter (inverting)\n"
        "LI 30 60 60 60 0\n"
        "LI 30 110 60 110 0\n"
        "MC 30 60 0 1 010\n"
        "FCJ\n"
        "TY 18 50 4 3 0 0 0 * +VIN\n"
        "TY 18 50 4 3 0 0 0 * \n"
        "MC 30 110 0 1 000\n"
        "FCJ\n"
        "TY 18 100 4 3 0 0 0 * GND\n"
        "TY 18 100 4 3 0 0 0 * \n"
        "LI 60 60 60 80 0\n"
        "LI 60 90 60 110 0\n"
        "MC 60 80 1 0 170\n"
        "FCJ\n"
        "TY 66 74 4 3 0 0 0 * CIN\n"
        "TY 66 79 4 3 0 0 0 * 100u\n"
        "SA 60 60 0\n"
        "SA 60 110 0\n",
        {
            "$ pi \"draw an inverting buck-boost converter\"",
            "╭─ pi ──────────────────────────────────────────╮",
            "│ Opening live GUI session:                     │",
            "│   fcai gui buck_boost.fcd --session-dir /tmp/ │",
            "│ Waiting for window... [connected]             │",
            "│                                               │",
            "│ ⚡ Step 1: Placing input terminals (+VIN, GND)│",
            "│    and input filter capacitor CIN (100uF)...  │",
            "│    session push -> OK                         │",
            "╰───────────────────────────────────────────────╯"
        },
        {},
        "push #1: added input stage and filter CIN"
    });

    // Step 2: Adding Q1 (NMOS) and L1 (Inductor)
    steps.push_back({
        "[FIDOCAD]\n"
        "TY 20 10 8 6 0 0 11 * Buck-boost converter (inverting)\n"
        "LI 30 60 60 60 0\n"
        "LI 60 60 90 60 0\n"
        "LI 110 60 140 60 0\n"
        "LI 30 110 140 110 0\n"
        "MC 30 60 0 1 010\n"
        "FCJ\n"
        "TY 18 50 4 3 0 0 0 * +VIN\n"
        "TY 18 50 4 3 0 0 0 * \n"
        "MC 30 110 0 1 000\n"
        "FCJ\n"
        "TY 18 100 4 3 0 0 0 * GND\n"
        "TY 18 100 4 3 0 0 0 * \n"
        "LI 60 60 60 80 0\n"
        "LI 60 90 60 110 0\n"
        "MC 60 80 1 0 170\n"
        "FCJ\n"
        "TY 66 74 4 3 0 0 0 * CIN\n"
        "TY 66 79 4 3 0 0 0 * 100u\n"
        "MC 100 75 3 0 410\n"
        "FCJ\n"
        "TY 83 46 4 3 0 0 0 * Q1\n"
        "TY 83 51 4 3 0 0 0 * NMOS\n"
        "LI 100 75 100 95 0\n"
        "MC 100 95 1 0 000\n"
        "FCJ\n"
        "TY 104 97 4 3 0 0 0 * PWM\n"
        "TY 104 97 4 3 0 0 0 * \n"
        "MC 140 80 1 0 120\n"
        "FCJ\n"
        "TY 146 72 4 3 0 0 0 * L1\n"
        "TY 146 77 4 3 0 0 0 * 100u\n"
        "LI 140 60 140 80 0\n"
        "LI 140 90 140 110 0\n"
        "SA 60 60 0\n"
        "SA 60 110 0\n"
        "SA 140 60 0\n"
        "SA 140 110 0\n",
        {
            "$ pi \"draw an inverting buck-boost converter\"",
            "╭─ pi ──────────────────────────────────────────╮",
            "│ ⚡ Step 1: Input stage and CIN... done         │",
            "│                                               │",
            "│ ⚡ Step 2: Placing power switch Q1 (NMOS)     │",
            "│    and energy storage inductor L1 (100uH)...  │",
            "│    session push -> OK                         │",
            "╰───────────────────────────────────────────────╯"
        },
        {},
        "push #2: added NMOS switch Q1 and inductor L1"
    });

    // Step 3: Adding D1, COUT, RL, -VOUT
    steps.push_back({
        "[FIDOCAD]\n"
        "TY 20 10 8 6 0 0 11 * Buck-boost converter (inverting)\n"
        "LI 30 60 60 60 0\n"
        "LI 60 60 90 60 0\n"
        "LI 110 60 140 60 0\n"
        "LI 140 60 170 60 0\n"
        "LI 185 60 275 60 0\n"
        "LI 30 110 275 110 0\n"
        "MC 30 60 0 1 010\n"
        "FCJ\n"
        "TY 18 50 4 3 0 0 0 * +VIN\n"
        "TY 18 50 4 3 0 0 0 * \n"
        "MC 30 110 0 1 000\n"
        "FCJ\n"
        "TY 18 100 4 3 0 0 0 * GND\n"
        "TY 18 100 4 3 0 0 0 * \n"
        "LI 60 60 60 80 0\n"
        "LI 60 90 60 110 0\n"
        "MC 60 80 1 0 170\n"
        "FCJ\n"
        "TY 66 74 4 3 0 0 0 * CIN\n"
        "TY 66 79 4 3 0 0 0 * 100u\n"
        "MC 100 75 3 0 410\n"
        "FCJ\n"
        "TY 83 46 4 3 0 0 0 * Q1\n"
        "TY 83 51 4 3 0 0 0 * NMOS\n"
        "LI 100 75 100 95 0\n"
        "MC 100 95 1 0 000\n"
        "FCJ\n"
        "TY 104 97 4 3 0 0 0 * PWM\n"
        "TY 104 97 4 3 0 0 0 * \n"
        "MC 140 80 1 0 120\n"
        "FCJ\n"
        "TY 146 72 4 3 0 0 0 * L1\n"
        "TY 146 77 4 3 0 0 0 * 100u\n"
        "LI 140 60 140 80 0\n"
        "LI 140 90 140 110 0\n"
        "MC 185 60 2 0 200\n"
        "FCJ\n"
        "TY 173 46 4 3 0 0 0 * D1\n"
        "TY 173 51 4 3 0 0 0 * Schottky\n"
        "MC 215 80 1 0 170\n"
        "FCJ\n"
        "TY 221 74 4 3 0 0 0 * COUT\n"
        "TY 221 79 4 3 0 0 0 * 220u\n"
        "LI 215 60 215 80 0\n"
        "LI 215 90 215 110 0\n"
        "MC 245 80 1 0 080\n"
        "FCJ\n"
        "TY 251 74 4 3 0 0 0 * RL\n"
        "TY 251 79 4 3 0 0 0 * 1k\n"
        "LI 245 60 245 80 0\n"
        "LI 245 90 245 110 0\n"
        "MC 275 60 0 0 020\n"
        "FCJ\n"
        "TY 275 50 4 3 0 0 0 * -VOUT\n"
        "TY 275 50 4 3 0 0 0 * \n"
        "MC 275 110 0 0 000\n"
        "FCJ\n"
        "TY 278 100 4 3 0 0 0 * GND\n"
        "TY 278 100 4 3 0 0 0 * \n"
        "SA 60 60 0\n"
        "SA 60 110 0\n"
        "SA 140 60 0\n"
        "SA 140 110 0\n"
        "SA 215 60 0\n"
        "SA 215 110 0\n"
        "SA 245 60 0\n"
        "SA 245 110 0\n",
        {
            "$ pi \"draw an inverting buck-boost converter\"",
            "╭─ pi ──────────────────────────────────────────╮",
            "│ ⚡ Step 2: Q1 and L1 placed... done            │",
            "│                                               │",
            "│ ⚡ Step 3: Adding Schottky diode D1, COUT,     │",
            "│    load resistor RL, and -VOUT terminal...    │",
            "│    session push -> OK                         │",
            "╰───────────────────────────────────────────────╯"
        },
        {},
        "push #3: added output stage D1, COUT, RL, -VOUT"
    });

    // Step 4: Adding formula & full wiring
    steps.push_back({
        "[FIDOCAD]\n"
        "TY 20 10 8 6 0 0 11 * Buck-boost converter (inverting)\n"
        "TY 20 130 4 3 0 0 0 * Vout = -Vin * D / (1 - D)\n"
        "LI 30 60 60 60 0\n"
        "LI 60 60 90 60 0\n"
        "LI 110 60 140 60 0\n"
        "LI 140 60 170 60 0\n"
        "LI 185 60 275 60 0\n"
        "LI 60 60 60 80 0\n"
        "LI 60 90 60 110 0\n"
        "LI 140 60 140 80 0\n"
        "LI 140 90 140 110 0\n"
        "LI 215 60 215 80 0\n"
        "LI 215 90 215 110 0\n"
        "LI 245 60 245 80 0\n"
        "LI 245 90 245 110 0\n"
        "LI 30 110 275 110 0\n"
        "MC 30 60 0 1 010\n"
        "FCJ\n"
        "TY 18 50 4 3 0 0 0 * +VIN\n"
        "TY 18 50 4 3 0 0 0 * \n"
        "MC 60 80 1 0 170\n"
        "FCJ\n"
        "TY 66 74 4 3 0 0 0 * CIN\n"
        "TY 66 79 4 3 0 0 0 * 100u\n"
        "MC 100 75 3 0 410\n"
        "FCJ\n"
        "TY 83 46 4 3 0 0 0 * Q1\n"
        "TY 83 51 4 3 0 0 0 * NMOS\n"
        "LI 100 75 100 95 0\n"
        "MC 100 95 1 0 000\n"
        "FCJ\n"
        "TY 104 97 4 3 0 0 0 * PWM\n"
        "TY 104 97 4 3 0 0 0 * \n"
        "MC 140 80 1 0 120\n"
        "FCJ\n"
        "TY 146 72 4 3 0 0 0 * L1\n"
        "TY 146 77 4 3 0 0 0 * 100u\n"
        "MC 185 60 2 0 200\n"
        "FCJ\n"
        "TY 173 46 4 3 0 0 0 * D1\n"
        "TY 173 51 4 3 0 0 0 * Schottky\n"
        "MC 215 80 1 0 170\n"
        "FCJ\n"
        "TY 221 74 4 3 0 0 0 * COUT\n"
        "TY 221 79 4 3 0 0 0 * 220u\n"
        "MC 245 80 1 0 080\n"
        "FCJ\n"
        "TY 251 74 4 3 0 0 0 * RL\n"
        "TY 251 79 4 3 0 0 0 * 1k\n"
        "MC 275 60 0 0 020\n"
        "FCJ\n"
        "TY 275 50 4 3 0 0 0 * -VOUT\n"
        "TY 275 50 4 3 0 0 0 * \n"
        "MC 30 110 0 1 000\n"
        "FCJ\n"
        "TY 18 100 4 3 0 0 0 * GND\n"
        "TY 18 100 4 3 0 0 0 * \n"
        "MC 275 110 0 0 000\n"
        "FCJ\n"
        "TY 278 100 4 3 0 0 0 * GND\n"
        "TY 278 100 4 3 0 0 0 * \n"
        "SA 60 60 0\n"
        "SA 140 60 0\n"
        "SA 60 110 0\n"
        "SA 140 110 0\n"
        "SA 215 60 0\n"
        "SA 215 110 0\n"
        "SA 245 60 0\n"
        "SA 245 110 0\n",
        {
            "$ pi \"draw an inverting buck-boost converter\"",
            "╭─ pi ──────────────────────────────────────────╮",
            "│ ⚡ Step 4: Connecting rails, junction dots,    │",
            "│    and transfer function formula:             │",
            "│    Vout = -Vin * D / (1 - D)                  │",
            "│                                               │",
            "│ 👂 Listening for user feedback in GUI...      │",
            "╰───────────────────────────────────────────────╯"
        },
        {},
        "push #4: completed wiring and transfer formula"
    });

    // Step 5: User gives feedback: "gate wire is too long"
    steps.push_back({
        steps[4].fcd,
        {
            "╭─ pi ──────────────────────────────────────────╮",
            "│ 👂 Listening for user feedback in GUI...      │",
            "│                                               │",
            "│ 💬 User (voice):                              │",
            "│    \"The gate wire is too long. Mount the PWM  │",
            "│     flag directly on the NMOS gate pin.\"     │",
            "│                                               │",
            "│ 🔍 Received selection: [LI 100 75 100 95]     │",
            "╰───────────────────────────────────────────────╯"
        },
        {19, 20}, // highlight gate wire & flag
        "User feedback: shorten gate lead, mount PWM directly"
    });

    // Step 6: Pi applies edit
    steps.push_back({
        "[FIDOCAD]\n"
        "TY 20 10 8 6 0 0 11 * Buck-boost converter (inverting)\n"
        "TY 20 130 4 3 0 0 0 * Vout = -Vin * D / (1 - D)\n"
        "LI 30 60 60 60 0\n"
        "LI 60 60 90 60 0\n"
        "LI 110 60 140 60 0\n"
        "LI 140 60 170 60 0\n"
        "LI 185 60 275 60 0\n"
        "LI 60 60 60 80 0\n"
        "LI 60 90 60 110 0\n"
        "LI 140 60 140 80 0\n"
        "LI 140 90 140 110 0\n"
        "LI 215 60 215 80 0\n"
        "LI 215 90 215 110 0\n"
        "LI 245 60 245 80 0\n"
        "LI 245 90 245 110 0\n"
        "LI 30 110 275 110 0\n"
        "MC 30 60 0 1 010\n"
        "FCJ\n"
        "TY 18 50 4 3 0 0 0 * +VIN\n"
        "TY 18 50 4 3 0 0 0 * \n"
        "MC 60 80 1 0 170\n"
        "FCJ\n"
        "TY 66 74 4 3 0 0 0 * CIN\n"
        "TY 66 79 4 3 0 0 0 * 100u\n"
        "MC 100 75 3 0 410\n"
        "FCJ\n"
        "TY 83 46 4 3 0 0 0 * Q1\n"
        "TY 83 51 4 3 0 0 0 * NMOS\n"
        "MC 140 80 1 0 120\n"
        "FCJ\n"
        "TY 146 72 4 3 0 0 0 * L1\n"
        "TY 146 77 4 3 0 0 0 * 100u\n"
        "MC 185 60 2 0 200\n"
        "FCJ\n"
        "TY 173 46 4 3 0 0 0 * D1\n"
        "TY 173 51 4 3 0 0 0 * Schottky\n"
        "MC 215 80 1 0 170\n"
        "FCJ\n"
        "TY 221 74 4 3 0 0 0 * COUT\n"
        "TY 221 79 4 3 0 0 0 * 220u\n"
        "MC 245 80 1 0 080\n"
        "FCJ\n"
        "TY 251 74 4 3 0 0 0 * RL\n"
        "TY 251 79 4 3 0 0 0 * 1k\n"
        "MC 100 75 1 0 000\n"
        "FCJ\n"
        "TY 104 77 4 3 0 0 0 * PWM\n"
        "TY 104 77 4 3 0 0 0 * \n"
        "MC 275 60 0 0 020\n"
        "FCJ\n"
        "TY 275 50 4 3 0 0 0 * -VOUT\n"
        "TY 275 50 4 3 0 0 0 * \n"
        "MC 30 110 0 1 000\n"
        "FCJ\n"
        "TY 18 100 4 3 0 0 0 * GND\n"
        "TY 18 100 4 3 0 0 0 * \n"
        "MC 275 110 0 0 000\n"
        "FCJ\n"
        "TY 278 100 4 3 0 0 0 * GND\n"
        "TY 278 100 4 3 0 0 0 * \n"
        "SA 60 60 0\n"
        "SA 140 60 0\n"
        "SA 60 110 0\n"
        "SA 140 110 0\n"
        "SA 215 60 0\n"
        "SA 215 110 0\n"
        "SA 245 60 0\n"
        "SA 245 110 0\n",
        {
            "╭─ pi ──────────────────────────────────────────╮",
            "│ 💬 User (voice): \"Gate wire too long...\"       │",
            "│                                               │",
            "│ 🛠️  Executing edit:                           │",
            "│    Removed gate extension wire                │",
            "│    Mounted PWM flag directly at (100, 75)     │",
            "│    Guarded push with base snapshot -> OK      │",
            "│    style: remembered \"mount flags on pin\"     │",
            "╰───────────────────────────────────────────────╯"
        },
        {},
        "ack: mounted PWM flag directly on gate pin"
    });

    // Step 7: Final verification & ground rail
    QString fullFcd;
    {
        QFile f("playground/buck-boost/buck_boost.fcd");
        if (f.open(QIODevice::ReadOnly)) fullFcd = QString::fromUtf8(f.readAll());
    }
    steps.push_back({
        fullFcd,
        {
            "╭─ pi ──────────────────────────────────────────╮",
            "│ 🛠️  Edit applied and saved                     │",
            "│                                               │",
            "│ ✅ fcai validate: round-trip ok (36 prims)     │",
            "│ ✅ fcai lint: 0 warnings, orthogonal nets     │",
            "│ 📁 Saved: buck_boost.fcd                      │",
            "│ 🎨 Rendered: buck_boost.png                   │",
            "│                                               │",
            "│ Circuit ready! Pair-programming complete.     │",
            "╰───────────────────────────────────────────────╯"
        },
        {},
        "Done · 36 primitives · round-trip ok"
    });

    // Render each step composite:
    // Frame size: 1200 x 580
    // Left: Terminal (480 x 540)
    // Right: GUI Window (680 x 540)
    for (size_t i = 0; i < steps.size(); i++) {
        const StepInfo &st = steps[i];
        QPixmap frame(1200, 580);
        frame.fill(QColor(15, 17, 21)); // Dark canvas background

        QPainter p(&frame);
        p.setRenderHint(QPainter::Antialiasing);

        // 1. Draw Terminal on the left: rect (20, 20, 480, 540)
        QRect termRect(20, 20, 480, 540);
        drawMacWindow(p, termRect, "pi — zsh", true);

        // Draw terminal text
        QFont monoFont("Menlo", 10);
        monoFont.setStyleHint(QFont::Monospace);
        p.setFont(monoFont);

        int textY = termRect.top() + 65;
        for (const QString &line : st.terminalLines) {
            if (line.startsWith("$")) {
                p.setPen(QColor(100, 220, 100)); // Green prompt
            } else if (line.contains("User (voice)")) {
                p.setPen(QColor(255, 190, 80)); // Amber user voice
            } else if (line.contains("✅") || line.contains("Circuit ready")) {
                p.setPen(QColor(120, 230, 150)); // Bright green
            } else if (line.contains("⚡") || line.contains("Step")) {
                p.setPen(QColor(110, 180, 255)); // Cyan / blue
            } else {
                p.setPen(QColor(200, 205, 215)); // Gray text
            }
            p.drawText(termRect.left() + 20, textY, line);
            textY += 21;
        }

        // 2. Draw GUI Window on the right: rect (510, 20, 670, 540)
        QRect guiRect(510, 20, 670, 540);
        drawMacWindow(p, guiRect, "buck_boost.fcd — fcai", true);

        // Render the actual GUI canvas into the window area
        {
            Settings settings("");
            settings.set("statusbar", "1");
            Theme theme = Theme::load("dark");
            Document doc;
            doc.loadLibraries(true, {}, {}, argv[0]);
            QString err;
            QTemporaryDir tmpDir;
            QString tmpFcd = tmpDir.path() + "/step.fcd";
            QFile f(tmpFcd);
            if (!f.open(QIODevice::WriteOnly)) continue;
            f.write(st.fcd.toUtf8());
            f.close();

            if (doc.open(tmpFcd, &err)) {
                MainWindow win(&doc, &settings, &theme, &app, "");
                win.resize(668, 504);
                if (!st.selection.isEmpty()) {
                    win.canvas()->setSelection(st.selection);
                }
                win.show();
                QTest::qWait(150);
                QPixmap guiShot = win.grab();

                // Blit into guiRect below the titlebar
                QRect targetCanvas(guiRect.left() + 1, guiRect.top() + 35, guiRect.width() - 2, guiRect.height() - 36);
                p.drawPixmap(targetCanvas, guiShot.scaled(targetCanvas.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            }
        }

        p.end();

        QString framePath = QString("/tmp/fcai_frames/frame_%1.png").arg((int)i, 2, 10, QChar('0'));
        frame.save(framePath);
        printf("Saved frame %d -> %s\n", (int)i, qPrintable(framePath));
    }

    printf("Compiling GIF with ImageMagick...\n");
    system("magick -delay 180 /tmp/fcai_frames/frame_00.png "
           "-delay 140 /tmp/fcai_frames/frame_01.png "
           "-delay 140 /tmp/fcai_frames/frame_02.png "
           "-delay 140 /tmp/fcai_frames/frame_03.png "
           "-delay 160 /tmp/fcai_frames/frame_04.png "
           "-delay 260 /tmp/fcai_frames/frame_05.png "
           "-delay 180 /tmp/fcai_frames/frame_06.png "
           "-delay 360 /tmp/fcai_frames/frame_07.png "
           "-loop 0 docs/images/session_pi_demo.gif");

    printf("Done generating assets!\n");
    return 0;
}
