// SPDX-License-Identifier: GPL-3.0-or-later
#include "parameter_panel.hpp"

#include "display_format.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/rocket.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {

// A QSlider only holds integers, so it counts steps from the minimum:
// value = min + steps · step.
int toSliderSteps(const orbit::ParameterInfo& p, double value) {
    return static_cast<int>(std::lround((value - p.min) / p.step));
}

double fromSliderSteps(const orbit::ParameterInfo& p, int steps) { return p.min + steps * p.step; }

int decimalsFor(double step) {
    if (step >= 1.0) return 0;
    if (step >= 0.1) return 1;
    return 2;
}

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

ParameterPanel::ParameterPanel(QWidget* parent)
    : QWidget(parent), m_mission(orbit::defaultMission()) {
    auto* layout = new QVBoxLayout(this);

    // One group box per heading, in the order the headings first appear.
    QGroupBox* group = nullptr;
    QGridLayout* grid = nullptr;
    std::string_view currentGroup;
    const auto params = orbit::missionParameters();
    m_rows.reserve(params.size());

    for (const orbit::ParameterInfo& p : params) {
        if (p.derived) continue; // e.g. burn time: shown under Derived, swept from the Sweep panel
        if (p.group != currentGroup) {
            currentGroup = p.group;
            group = new QGroupBox(toQString(p.group), this);
            grid = new QGridLayout(group);
            grid->setColumnStretch(0, 1);
            layout->addWidget(group);

            if (p.group == "Orbit insertion") {
                m_autoCircularize = new QCheckBox(tr("Auto-circularize at apoapsis"), group);
                m_autoCircularize->setObjectName(QStringLiteral("autoCircularize"));
                m_autoCircularize->setToolTip(
                    tr("Cut the engine when apoapsis reaches the target altitude, coast up, "
                       "then burn to make the orbit circular. Off: burn until the propellant "
                       "runs out."));
                grid->addWidget(m_autoCircularize, grid->rowCount(), 0, 1, 2);
                connect(m_autoCircularize, &QCheckBox::toggled, this, [this](bool on) {
                    m_mission.autoCircularize = on;
                    refreshWidgets();
                    emit missionChanged(m_mission);
                });
            }
        }

        auto* label = new QLabel(toQString(p.label), group);
        auto* spinBox = new QDoubleSpinBox(group);
        spinBox->setRange(p.min, p.max);
        spinBox->setSingleStep(p.step);
        spinBox->setDecimals(decimalsFor(p.step));
        spinBox->setSuffix(QLatin1Char(' ') + toQString(p.unit));
        spinBox->setKeyboardTracking(false); // update when Enter is pressed, not per keystroke
        spinBox->setAlignment(Qt::AlignRight);

        auto* slider = new QSlider(Qt::Horizontal, group);
        // Object names let tests (and accessibility tools) find each control.
        spinBox->setObjectName(QStringLiteral("spin-") + toQString(p.key));
        slider->setObjectName(QStringLiteral("slider-") + toQString(p.key));
        slider->setRange(0, toSliderSteps(p, p.max));

        const QString help = toQString(p.help);
        for (QWidget* w : {static_cast<QWidget*>(label), static_cast<QWidget*>(spinBox),
                           static_cast<QWidget*>(slider)}) {
            w->setToolTip(help);
        }

        const int row = grid->rowCount();
        grid->addWidget(label, row, 0);
        grid->addWidget(spinBox, row, 1);
        grid->addWidget(slider, row + 1, 0, 1, 2);
        m_rows.push_back({&p, label, slider, spinBox});

        // Keep slider and spin box in step; whichever the user moves wins.
        const std::size_t index = m_rows.size() - 1;
        connect(slider, &QSlider::valueChanged, this, [this, index](int steps) {
            const Row& r = m_rows[index];
            onValueEdited(r, fromSliderSteps(*r.info, steps));
        });
        connect(spinBox, &QDoubleSpinBox::valueChanged, this,
                [this, index](double value) { onValueEdited(m_rows[index], value); });
    }

    buildDerivedSection(layout);

    auto* reset = new QPushButton(tr("Reset to defaults"), this);
    connect(reset, &QPushButton::clicked, this, [this] {
        setMission(orbit::defaultMission());
        emit missionChanged(m_mission);
    });
    layout->addWidget(reset);
    layout->addStretch(1);

    refreshWidgets();
}

void ParameterPanel::buildDerivedSection(QBoxLayout* layout) {
    auto* group = new QGroupBox(tr("Derived"), this);
    auto* form = new QFormLayout(group);
    const auto addRow = [&](const QString& name, const QString& tooltip) {
        auto* value = new QLabel(group);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setToolTip(tooltip);
        form->addRow(name, value);
        return value;
    };
    m_burnTime = addRow(tr("Burn time"), tr("Propellant ÷ mass flow, at full thrust"));
    m_massFlow = addRow(tr("Mass flow"), tr("F / (Isp · g0)"));
    m_thrustToWeight =
        addRow(tr("Liftoff T/W"), tr("Thrust ÷ weight on the pad. Must be above 1 to lift off."));
    m_idealDeltaV = addRow(tr("Ideal Δv"), tr("Tsiolkovsky: Isp · g0 · ln(m0 / m_dry)"));
    m_rotationBoost = addRow(tr("Earth rotation boost"),
                             tr("Speed the launch pad already has along the launch direction"));
    layout->addWidget(group);
}

void ParameterPanel::setMission(const orbit::Mission& mission) {
    m_mission = mission;
    refreshWidgets();
}

void ParameterPanel::setLockedParameter(std::string_view key) {
    m_lockedKey = key == "burn-time" ? std::string("propellant") : std::string(key);
    refreshWidgets();
}

void ParameterPanel::onValueEdited(const Row& row, double value) {
    row.info->set(m_mission, value);
    refreshWidgets();
    emit missionChanged(m_mission);
}

/// Put the mission's values into every widget, without triggering their
/// change signals (that would feed back into onValueEdited).
void ParameterPanel::refreshWidgets() {
    for (const Row& r : m_rows) {
        const double value = r.info->get(m_mission);
        const QSignalBlocker blockSlider(r.slider);
        const QSignalBlocker blockSpin(r.spinBox);
        r.slider->setValue(toSliderSteps(*r.info, value));
        r.spinBox->setValue(value);

        // The target altitude only matters with auto-circularize on, and a
        // swept parameter is set by the sweep, not the slider.
        const bool locked = r.info->key == m_lockedKey;
        const bool active =
            !locked && (r.info->key != "target-altitude" || m_mission.autoCircularize);
        r.label->setEnabled(active);
        r.slider->setEnabled(active);
        r.spinBox->setEnabled(active);
        r.label->setToolTip(locked ? tr("Being swept: see the Sweep panel")
                                   : toQString(r.info->help));
    }
    const QSignalBlocker blockCheck(m_autoCircularize);
    m_autoCircularize->setChecked(m_mission.autoCircularize);
    refreshDerived();
}

void ParameterPanel::refreshDerived() {
    const orbit::Rocket& rocket = m_mission.rocket;
    m_burnTime->setText(display::duration(orbit::burnTime(rocket)));
    m_massFlow->setText(tr("%1 kg/s").arg(QLocale().toString(orbit::massFlowRate(rocket), 'f', 1)));

    const double twr = orbit::liftoffThrustToWeight(rocket);
    const QString twrText = QLocale().toString(twr, 'f', 2);
    m_thrustToWeight->setText(
        twr > 1.0 ? twrText
                  : tr("<span style='color:#f87171'>%1 (can't lift off)</span>").arg(twrText));
    m_idealDeltaV->setText(display::speed(orbit::idealDeltaV(rocket)));

    // The pad's velocity, projected onto the launch direction.
    const orbit::Vec3 pad = orbit::positionFromLatLon(
        m_mission.site.latitude, m_mission.site.longitude, orbit::kRadiusEarth);
    const orbit::Vec3 heading =
        orbit::horizontalDirection(orbit::localFrame(pad), m_mission.guidance.azimuth);
    const double boost = orbit::dot(orbit::groundVelocity(pad), heading);
    m_rotationBoost->setText((boost >= 0.0 ? QStringLiteral("+") : QString()) +
                             display::speed(boost));
}
