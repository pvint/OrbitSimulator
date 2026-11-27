// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/flyby.hpp"
#include "orbit/core/lost_in_space.hpp"
#include "orbit/core/voyage.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

/// The state of one game of "Lost in space": the puzzle, the player's
/// planned burns, and the game clock. Burns before the clock have fired and
/// are locked; burns after it can still be changed.
///
/// Everything slow (making a puzzle, predicting the path, working out a
/// hint) runs on worker threads; the results come back as signals on the
/// GUI thread. Predictions are "latest wins", like SimulationController's.
class GameSession : public QObject {
    Q_OBJECT

public:
    explicit GameSession(QObject* parent = nullptr);
    ~GameSession() override;
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    GameSession(GameSession&&) = delete;
    GameSession& operator=(GameSession&&) = delete;

    /// Make a new puzzle starting at `epoch` (in the background:
    /// puzzleChanged follows).
    void newPuzzle(const orbit::PuzzleSpec& spec, std::uint64_t seed, double epoch);
    /// Start this puzzle now (tests, and replaying one).
    void setPuzzle(const orbit::Puzzle& puzzle);

    bool hasPuzzle() const { return m_hasPuzzle; }
    /// The puzzle being played (a default one before the first).
    const orbit::Puzzle& puzzle() const { return m_puzzle; }
    bool isBusy() const;

    /// The game clock (s since J2000).
    double now() const { return m_now; }
    double startEpoch() const { return m_puzzle.voyage.startEpoch; }
    /// Move the clock (not before the start). Burns it passes fire.
    void setNow(double epoch);
    /// Back to the start: every burn can be changed again.
    void restart();

    /// The burns, in time order.
    const std::vector<orbit::Burn>& burns() const { return m_burns; }
    int burnCount() const { return static_cast<int>(m_burns.size()); }
    bool isLocked(int index) const;
    int selected() const { return m_selected; }
    void select(int index);
    /// Add an empty burn at `epoch` (not before the clock) and select it.
    int addBurn(double epoch);
    /// Change a burn that hasn't fired. Its time may move (not before the
    /// clock); the list stays in time order and the selection follows it.
    void setBurn(int index, const orbit::Burn& burn);
    void removeBurn(int index);

    /// The puzzle's voyage with the player's burns.
    orbit::Voyage voyage() const;
    /// The latest prediction of the whole flight (null until the first).
    const orbit::VoyageResult* prediction() const { return m_prediction.get(); }
    /// The prediction is for the current burns (none pending).
    bool predictionCurrent() const { return m_predictionCurrent; }

    /// Work out the next sensible step and apply it (hintReady follows).
    void requestHint();
    /// Change burn `index` (one that hasn't fired) so the path passes a
    /// planet as `aim` says (in the background; aimReady follows with what
    /// happened). Not counted as a hint: it's a tool.
    void aimBurn(int index, const orbit::FlybyAim& aim);
    int hintsUsed() const { return m_hintsUsed; }

    /// Wait for background work (tests).
    void waitForIdle() const;

signals:
    void puzzleChanged();
    void burnsChanged();
    void selectionChanged(int index);
    void nowChanged(double epoch);
    void predictionChanged();
    /// A hint's explanation, for the player.
    void hintReady(const QString& message);
    /// What aiming a burn at a planet did (or why it couldn't).
    void aimReady(bool aimed, const QString& message);
    void busyChanged(bool busy);

private:
    struct Hint {
        std::vector<orbit::Burn> burns; ///< the new plan (empty: unchanged)
        int select = -1;
        QString message;
        bool counts = true; ///< counted as a hint
        /// Predictions must reach this epoch from now on (an aimed pass).
        double reach = 0.0;
    };

    void predict();
    void onPredicted();
    void onPuzzleMade();
    void onHintMade();
    static Hint computeHint(const orbit::Puzzle& puzzle, const std::vector<orbit::Burn>& burns,
                            double now);
    static Hint computeAim(const orbit::Puzzle& puzzle, const std::vector<orbit::Burn>& burns,
                           int index, const orbit::FlybyAim& aim);
    /// Parked around `moon`: the way out of it (orbit::planFromMoon), aimed,
    /// replacing the burns from `now` on (`planned` of them before).
    static std::optional<Hint> hintFromMoon(orbit::Voyage voyage, std::size_t planned,
                                            orbit::Body moon, double now);
    void sortBurns(int follow);

    /// The puzzle with the voyage's look-ahead stretched to reach m_reach.
    orbit::Puzzle workingPuzzle() const;

    orbit::Puzzle m_puzzle;
    double m_reach = 0.0; ///< predictions reach at least this epoch
    bool m_hasPuzzle = false;
    std::vector<orbit::Burn> m_burns;
    double m_now = 0.0;
    int m_selected = -1;
    int m_hintsUsed = 0;
    bool m_makingPuzzle = false;
    bool m_makingHint = false;

    std::shared_ptr<const orbit::VoyageResult> m_prediction;
    bool m_predictionCurrent = false;
    bool m_predictPending = false;
    QFutureWatcher<std::shared_ptr<const orbit::VoyageResult>> m_predictWatcher;
    QFutureWatcher<orbit::Puzzle> m_puzzleWatcher;
    QFutureWatcher<Hint> m_hintWatcher;
};
