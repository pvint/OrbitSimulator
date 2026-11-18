// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"
#include "orbit/core/version.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Orbit Simulator"));
    QApplication::setOrganizationName(QStringLiteral("OrbitSim"));
    QApplication::setApplicationVersion(
        QString::fromUtf8(orbit::kVersion.data(), static_cast<qsizetype>(orbit::kVersion.size())));
    // Lets Linux desktops (Wayland in particular) match the window to its
    // .desktop file, and so to its icon and name. Only when that file is
    // installed: otherwise (e.g. run from the build folder) Qt warns that
    // the desktop portal doesn't know the app.
    const QString desktopId = QStringLiteral(ORBIT_APP_ID);
    if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId + ".desktop")
             .isEmpty()) {
        QApplication::setDesktopFileName(desktopId);
    }
    QIcon icon;
    for (const int size : {16, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/icons/png/orbitsim-%1.png").arg(size), QSize(size, size));
    }
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::applicationName());
    parser.addHelpOption();
    parser.addVersionOption();
    // For documentation and visual checks: render the window to an image
    // file after a delay, then quit. Captures only this app's own window.
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a screenshot of the window to <file> "
                                                       "and quit."),
                                        QStringLiteral("file"));
    const QCommandLineOption delay(QStringLiteral("screenshot-delay"),
                                   QStringLiteral("Milliseconds to wait before the screenshot "
                                                  "(default 3000)."),
                                   QStringLiteral("ms"), QStringLiteral("3000"));
    const QCommandLineOption demo(QStringLiteral("demo"),
                                  QStringLiteral("Start the guided demo tour."));
    const QCommandLineOption demoStep(
        QStringLiteral("demo-step"), QStringLiteral("Start the demo tour at step <n> (1 = first)."),
        QStringLiteral("n"));
    parser.addOptions({screenshot, delay, demo, demoStep});
    parser.addPositionalArgument(QStringLiteral("scenario"),
                                 QStringLiteral("A scenario file to open (optional)."),
                                 QStringLiteral("[scenario]"));
    parser.process(app);

    MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        window.openScenarioFile(parser.positionalArguments().constFirst());
    }
    if (parser.isSet(demoStep)) {
        window.startDemo(std::max(0, parser.value(demoStep).toInt() - 1));
    } else if (parser.isSet(demo)) {
        window.startDemo();
    }

    if (parser.isSet(screenshot)) {
        const QString path = parser.value(screenshot);
        QTimer::singleShot(parser.value(delay).toInt(), &window, [&window, path] {
            const bool saved = window.grab().save(path);
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }
    return QApplication::exec();
}
