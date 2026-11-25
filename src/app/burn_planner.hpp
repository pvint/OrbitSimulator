// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class GameSession;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;

/// The game's side panel: the fuel gauge, the list of burns, and an editor
/// for the selected one (when, and how hard in each direction). Every change
/// goes straight to the GameSession, which predicts the new path.
class BurnPlanner : public QWidget {
    Q_OBJECT

public:
    explicit BurnPlanner(GameSession* session, QWidget* parent = nullptr);

private:
    /// One direction of the burn: a coarse slider and a precise spin box.
    struct Axis {
        QSlider* slider = nullptr;
        QDoubleSpinBox* spin = nullptr;
    };

    Axis addAxis(class QFormLayout* form, const QString& label, const QString& key,
                 const QString& help);
    void refresh();
    void refreshFuel();
    void editorChanged();

    GameSession* m_session;
    QProgressBar* m_fuel = nullptr;
    QLabel* m_fuelText = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_remove = nullptr;
    QPushButton* m_hint = nullptr;
    QGroupBox* m_editor = nullptr;
    QSlider* m_whenSlider = nullptr;
    QDoubleSpinBox* m_whenSpin = nullptr;
    QLabel* m_whenDate = nullptr;
    Axis m_prograde;
    Axis m_radial;
    Axis m_normal;
    QLabel* m_size = nullptr;
    bool m_updating = false;
};
