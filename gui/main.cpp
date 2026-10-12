/* fcai-gui — FidoCad editor for a human working with a coding agent.
 *
 * Usually the agent runs it in the background with a session directory and
 * then talks to the window through `fcai session ...`:
 *
 *   fcai-gui sketch.fcd --session-dir /tmp/dac.session -m "what to look at" &
 *   fcai session wait /tmp/dac.session     # user sent something (Return)
 *   fcai session push /tmp/dac.session new.fcd
 *
 * Return sends (saves the drawing, writes an event) and keeps the window
 * open; Ctrl+Return ends the session and prints a report on stdout. */

#include "document.h"
#include "settings.h"
#include "theme.h"
#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

namespace {

const char *kDescription =
    "FidoCad drawing editor for a human working with a coding agent.\n"
    "\n"
    "The agent runs it (usually in the background with --session-dir DIR) and\n"
    "talks to the window through `fcai session` (see `fcai session --help`).\n"
    "Return sends the drawing to the agent and keeps the window open; Ctrl+Return\n"
    "ends the session and prints the report.\n"
    "Exit status: 0 = done, 2 = window closed, 1 = error.\n"
    "\n"
    "  fcai-gui sketch.fcd --session-dir /tmp/div.session\n"
    "  cat draft.fcd | fcai-gui - --json\n"
    "\n"
    "Settings live in $XDG_CONFIG_HOME/fidocad/gui.conf and are changed with\n"
    "`fcai session set DIR key value` or by the agent at startup.";

void writeOut(const QByteArray &b) {
    fwrite(b.constData(), 1, (size_t)b.size(), stdout);
    fflush(stdout);
}

QString defaultSessionDir(const QString &file, bool from_stdin) {
    if (!from_stdin && !file.isEmpty())
        return QFileInfo(file).absolutePath() + "/." + QFileInfo(file).fileName() + ".fcai-session";
    QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (base.isEmpty()) base = QDir::tempPath();
    return base + "/fcai-gui.session";
}

} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("fcai-gui");
    QApplication::setApplicationVersion(FC_VERSION);
    QApplication::setDesktopFileName("fcai-gui");

    QCommandLineParser cli;
    cli.setApplicationDescription(kDescription);
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument("file", "Drawing to open (.fcd), or - for stdin.");
    QCommandLineOption sessionOpt("session-dir", "Session directory for `fcai session ...` (default: next to the drawing).", "dir");
    QCommandLineOption noSessionOpt("no-session", "Do not create a session channel.");
    QCommandLineOption titleOpt("title", "Window title prefix.", "text");
    QCommandLineOption jsonOpt("json", "Print the report as JSON.");
    QCommandLineOption noFcdOpt("no-fcd", "Leave the final drawing out of the report.");
    QCommandLineOption libOpt("lib", "Load a .fcl library (repeatable).", "path");
    QCommandLineOption libDirOpt("lib-dir", "Load every .fcl in a directory (repeatable).", "dir");
    QCommandLineOption noStdOpt("no-stdlib", "Do not load the standard libraries.");
    QCommandLineOption configOpt("config", "Settings file (default $XDG_CONFIG_HOME/fidocad/gui.conf).", "path");
    QCommandLineOption shotOpt("screenshot", "Save a PNG of the window after start-up and exit (for previews/tests).", "png");
    cli.addOptions({sessionOpt, noSessionOpt, titleOpt, jsonOpt, noFcdOpt, libOpt, libDirOpt,
                    noStdOpt, configOpt, shotOpt});
    cli.process(app);

    QByteArray stdlib_env = qgetenv("FIDO_NO_STDLIB");
    Settings settings(cli.value(configOpt));
    settings.load();
    Theme theme = Theme::load(settings.get("theme"));
    theme.apply(app, settings.get("font"), settings.getInt("font_size"));

    Document doc;
    doc.loadLibraries(!cli.isSet(noStdOpt) && stdlib_env.isEmpty(), cli.values(libOpt),
                      cli.values(libDirOpt), argv[0]);

    QString file = cli.positionalArguments().value(0);
    bool from_stdin = file == "-";
    if (from_stdin) {
        QFile in;
        if (!in.open(stdin, QIODevice::ReadOnly)) {
            fprintf(stderr, "fcai-gui: cannot read stdin\n");
            return 1;
        }
        doc.openText(in.readAll());
    } else if (!file.isEmpty()) {
        QString err;
        if (!doc.open(file, &err)) {
            fprintf(stderr, "fcai-gui: cannot open %s: %s\n", qPrintable(file), qPrintable(err));
            return 1;
        }
    }

    QString sessionDir;
    if (cli.isSet(sessionOpt)) sessionDir = cli.value(sessionOpt);
    else if (!cli.isSet(noSessionOpt)) sessionDir = defaultSessionDir(file, from_stdin);
    if (!cli.isSet(sessionOpt) && !cli.isSet(noSessionOpt) && !from_stdin && !file.isEmpty())
        fprintf(stderr, "fcai-gui: session: %s\n", qPrintable(sessionDir));

    MainWindow win(&doc, &settings, &theme, &app, sessionDir);
    if (!win.canvas()->backgroundError().isEmpty())
        fprintf(stderr, "fcai-gui: %s\n", qPrintable(win.canvas()->backgroundError()));
    if (cli.isSet(titleOpt)) win.setAgentTitle(cli.value(titleOpt));

    win.onFinished = [&app] { app.quit(); };
    if (!cli.isSet(shotOpt)) win.placeWindow();
    win.show();
    win.canvas()->setFocus();
    if (!cli.isSet(shotOpt)) QTimer::singleShot(100, &win, [&win] { win.bringToFront(); });

    if (cli.isSet(shotOpt)) {
        QString out = cli.value(shotOpt);
        QTimer::singleShot(600, &app, [&win, &app, out] {
            win.canvas()->fit();
            QTimer::singleShot(200, &app, [&win, &app, out] {
                QImage img = win.grab().toImage();
                img.save(out, "PNG");
                app.exit(3);
            });
        });
        int rc = app.exec();
        return rc == 3 ? 0 : 1;
    }

    app.exec();

    /* ---- report for the agent (also available live through the session) ---- */
    QString status = win.status();
    QString note = win.note();
    QList<int> sel = win.canvas()->selection();
    QByteArray fcd = doc.fcd();
    bool include_fcd = !cli.isSet(noFcdOpt);

    if (cli.isSet(jsonOpt)) {
        QJsonObject r;
        r["status"] = status;
        r["file"] = from_stdin ? QJsonValue() : QJsonValue(doc.path());
        r["saved"] = win.saved();
        if (!win.saveError().isEmpty()) r["save_error"] = win.saveError();
        r["note"] = note;
        r["session"] = sessionDir;
        QJsonArray js;
        for (int i : sel) js << i;
        r["selection"] = js;
        r["diff"] = QJsonDocument::fromJson(doc.diff(true).toUtf8()).object();
        if (include_fcd) r["fcd"] = QString::fromUtf8(fcd);
        writeOut(QJsonDocument(r).toJson(QJsonDocument::Indented));
    } else {
        QString o;
        QTextStream t(&o);
        t << "status: " << status << (status == "done" ? "" : " (user closed the window without Done)") << "\n";
        if (from_stdin) t << "file: (stdin)\n";
        else if (!doc.path().isEmpty())
            t << "file: " << doc.path() << (win.saved() ? " (saved)" : doc.modified() ? " (NOT saved)" : "")
              << (win.saveError().isEmpty() ? "" : " error: " + win.saveError()) << "\n";
        if (!sessionDir.isEmpty()) t << "session: " << sessionDir << "\n";
        t << "note: " << (note.isEmpty() ? "(none)" : note) << "\n";
        if (!sel.isEmpty()) {
            QStringList s;
            for (int i : sel) s << QString::number(i);
            t << "selection: [" << s.join(", ") << "]  (primitives the user left selected)\n";
        }
        t << "\n--- changes since opened ---\n" << doc.diff(false);
        if (include_fcd) t << "\n--- final drawing ---\n" << QString::fromUtf8(fcd);
        t.flush();
        writeOut(o.toUtf8());
    }
    return status == "done" ? 0 : 2;
}
