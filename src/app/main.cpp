// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_session.hpp"
#include "game_window.hpp"
#include "main_window.hpp"
#include "orbit/core/version.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {

void saveScreenshotLater(QWidget* window, const QString& path, int delayMs) {
    QTimer::singleShot(delayMs, window, [window, path] {
        const bool saved = window->grab().save(path);
        QCoreApplication::exit(saved ? 0 : 1);
    });
}

/// The game in a window of its own (--game, --puzzle).
struct GameOptions {
    QCommandLineOption puzzle;
    QCommandLineOption hint;
    QCommandLineOption noCards;
    QCommandLineOption screenshot;
    QCommandLineOption delay;
};

int runGame(const QCommandLineParser& parser, const GameOptions& o) {
    GameWindow window;
    window.setCardsEnabled(!parser.isSet(o.noCards));
    window.show();
    if (parser.isSet(o.puzzle)) {
        const QStringList parts = parser.value(o.puzzle).split(QLatin1Char(':'));
        const QString level = parts.value(0).toLower();
        orbit::Difficulty difficulty = orbit::Difficulty::Normal;
        if (level == QLatin1String("easy")) difficulty = orbit::Difficulty::Easy;
        if (level == QLatin1String("hard")) difficulty = orbit::Difficulty::Hard;
        const std::uint64_t seed =
            parts.size() > 1 ? parts.at(1).toULongLong() : QRandomGenerator::global()->generate64();
        window.startPuzzle(difficulty, seed);
        if (parser.isSet(o.hint)) {
            QObject::connect(
                window.session(), &GameSession::puzzleChanged, &window,
                [&window] { window.session()->requestHint(); }, Qt::SingleShotConnection);
        }
    }
    if (parser.isSet(o.screenshot)) {
        saveScreenshotLater(&window, parser.value(o.screenshot), parser.value(o.delay).toInt());
    }
    return QApplication::exec();
}

} // namespace

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
    const QCommandLineOption game(QStringLiteral("game"),
                                  QStringLiteral("Open the Lost in Space game on its own."));
    const QCommandLineOption puzzle(
        QStringLiteral("puzzle"),
        QStringLiteral("Start the game with this puzzle: easy, normal or hard, and optionally "
                       "a seed (normal:42)."),
        QStringLiteral("puzzle"));
    const QCommandLineOption hint(QStringLiteral("hint"),
                                  QStringLiteral("With --puzzle: take a hint straight away."));
    const QCommandLineOption noCards(QStringLiteral("no-cards"),
                                     QStringLiteral("In the game, skip the message cards "
                                                    "(for screenshots)."));
    parser.addOptions({screenshot, delay, demo, demoStep, game, puzzle, hint, noCards});
    parser.addPositionalArgument(QStringLiteral("scenario"),
                                 QStringLiteral("A scenario file to open (optional)."),
                                 QStringLiteral("[scenario]"));
    parser.process(app);

    if (parser.isSet(game) || parser.isSet(puzzle)) {
        return runGame(parser, {puzzle, hint, noCards, screenshot, delay});
    }

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
        saveScreenshotLater(&window, parser.value(screenshot), parser.value(delay).toInt());
    }
    return QApplication::exec();
}
