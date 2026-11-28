// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_session.hpp"
#include "game_window.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/version.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>

namespace {

void saveScreenshotLater(QWidget* window, const QString& path, int delayMs) {
    QTimer::singleShot(delayMs, window, [window, path] {
        const bool saved = window->grab().save(path);
        QCoreApplication::exit(saved ? 0 : 1);
    });
}

/// The game: the app's one window (--puzzle starts one straight away).
struct GameOptions {
    QCommandLineOption puzzle;
    QCommandLineOption freeRoam;
    QCommandLineOption hint;
    QCommandLineOption centre;
    QCommandLineOption noCards;
    QCommandLineOption screenshot;
    QCommandLineOption delay;
};

int runGame(const QCommandLineParser& parser, const GameOptions& o) {
    GameWindow window;
    window.setCardsEnabled(!parser.isSet(o.noCards));
    window.show();
    if (parser.isSet(o.freeRoam)) {
        const auto body = orbit::bodyFromKey(parser.value(o.freeRoam).toLower().toStdString());
        window.startFreeRoam(body.value_or(orbit::Body::Earth));
    } else if (parser.isSet(o.puzzle)) {
        // Any order: a difficulty, where from (a place, or a moon to start
        // in orbit around), "orbit" to start in orbit there, where to (a
        // second planet, or a moon), a seed (e.g. "hard:jupiter:4",
        // "normal:earth:saturn", "easy:mars:orbit", "normal:earth:moon",
        // "hard:jupiter:orbit:europa", "normal:moon:earth",
        // "hard:ganymede:europa").
        orbit::PuzzleSpec spec;
        spec.difficulty = orbit::Difficulty::Normal;
        bool haveFrom = false;
        std::uint64_t seed = QRandomGenerator::global()->generate64();
        for (const QString& part : parser.value(o.puzzle).split(QLatin1Char(':'))) {
            const QString word = part.toLower();
            bool isNumber = false;
            const std::uint64_t number = word.toULongLong(&isNumber);
            if (isNumber) {
                seed = number;
            } else if (word == QLatin1String("easy")) {
                spec.difficulty = orbit::Difficulty::Easy;
            } else if (word == QLatin1String("hard")) {
                spec.difficulty = orbit::Difficulty::Hard;
            } else if (word == QLatin1String("orbit")) {
                spec.inOrbit = true;
            } else if (const auto r = orbit::regionFromKey(word.toStdString())) {
                const auto planet = orbit::regionPlanet(*r);
                if (!haveFrom) {
                    spec.from = *r;
                    haveFrom = true;
                } else if (planet) {
                    spec.to = *planet;
                }
            } else if (const auto moon = orbit::bodyFromKey(word.toStdString());
                       moon && orbit::isMoon(*moon)) {
                if (!haveFrom) {
                    spec.startMoon = moon;
                    haveFrom = true;
                } else {
                    spec.to = *moon;
                }
            }
        }
        window.startPuzzle(spec, seed);
        if (parser.isSet(o.hint)) {
            QObject::connect(
                window.session(), &GameSession::puzzleChanged, &window,
                [&window] { window.session()->requestHint(); }, Qt::SingleShotConnection);
        }
    }
    if (const auto body = orbit::bodyFromKey(parser.value(o.centre).toStdString())) {
        const auto centre = [&window, body] { window.centreOnBody(*body); };
        if (window.session()->hasPuzzle()) {
            centre(); // free roam starts at once
        } else {
            QObject::connect(window.session(), &GameSession::puzzleChanged, &window, centre,
                             Qt::SingleShotConnection);
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
    // Accepted for old shortcuts and scripts: the game is the app now.
    const QCommandLineOption game(QStringLiteral("game"),
                                  QStringLiteral("Open the game (the default)."));
    const QCommandLineOption puzzle(
        QStringLiteral("puzzle"),
        QStringLiteral("Start the game with this puzzle, in any order: easy, normal or hard; "
                       "where from (between, mercury, venus, earth, mars, belt, jupiter, "
                       "saturn, uranus, neptune); \"orbit\" to start in orbit there; where "
                       "to (a second planet or a moon, else the Earth); a seed. Where from "
                       "can be a moon, to start in orbit around it (hard:jupiter:42, "
                       "normal:earth:saturn, easy:mars:orbit, normal:earth:moon, "
                       "hard:earth:titan, normal:moon:earth, hard:ganymede:europa)."),
        QStringLiteral("puzzle"));
    const QCommandLineOption freeRoam(
        QStringLiteral("free-roam"),
        QStringLiteral("Start free roam (no goal, unlimited fuel) in orbit around this planet or "
                       "moon (earth, moon, mars, jupiter, titan, ...)."),
        QStringLiteral("body"));
    const QCommandLineOption hint(QStringLiteral("hint"),
                                  QStringLiteral("With --puzzle: take a hint straight away."));
    const QCommandLineOption centre(QStringLiteral("centre"),
                                    QStringLiteral("With --puzzle or --free-roam: centre the view "
                                                   "on this planet or moon (saturn, titan)."),
                                    QStringLiteral("body"));
    const QCommandLineOption noCards(QStringLiteral("no-cards"),
                                     QStringLiteral("In the game, skip the message cards "
                                                    "(for screenshots)."));
    parser.addOptions({screenshot, delay, game, puzzle, freeRoam, hint, centre, noCards});
    parser.process(app);
    return runGame(parser, {puzzle, freeRoam, hint, centre, noCards, screenshot, delay});
}
