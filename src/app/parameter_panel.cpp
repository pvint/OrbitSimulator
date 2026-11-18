// SPDX-License-Identifier: GPL-3.0-or-later
#include "parameter_panel.hpp"

#include "display_format.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/lunar_targeting.hpp"
#include "orbit/core/rocket.hpp"

#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
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

/// Does this parameter make a difference with this flight plan?
bool appliesToPlan(const orbit::ParameterInfo& p, orbit::FlightPlan plan) {
    using orbit::FlightPlan;
    const bool lunar = plan == FlightPlan::LunarFlyby || plan == FlightPlan::LunarOrbit;
    // A custom program carries its own numbers (altitudes, timings).
    if (p.key == "target-altitude") {
        return plan != FlightPlan::BurnToDepletion && plan != FlightPlan::Custom;
    }
    if (p.key == "capture-apoapsis") return plan == FlightPlan::LunarOrbit;
    if (p.group == "Lunar transfer") return lunar;
    return true;
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

            if (p.group == "Launch site") {
                // The date has no slider: it's a calendar, not a number.
                m_launchDate = new QDateEdit(group);
                m_launchDate->setObjectName(QStringLiteral("launchDate"));
                m_launchDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
                m_launchDate->setCalendarPopup(true);
                m_launchDate->setDateRange(QDate(1900, 1, 1), QDate(2100, 12, 31));
                m_launchDate->setToolTip(
                    tr("Launch date (UTC). With the launch time it sets where the Moon "
                       "and the Sun are."));
                const int row = grid->rowCount();
                grid->addWidget(new QLabel(tr("Launch date"), group), row, 0);
                grid->addWidget(m_launchDate, row, 1);
                connect(m_launchDate, &QDateEdit::dateChanged, this, [this](QDate date) {
                    const double hour = orbit::launchHourOfDay(m_mission);
                    m_mission.launchEpoch = orbit::epochFromCalendar({date.year(), date.month(),
                                                                      date.day(), 0, 0, 0.0}) +
                                            hour * 3600.0;
                    refreshWidgets();
                    emit missionChanged(m_mission);
                });
            }
            if (p.group == "Orbit insertion") {
                m_plan = new QComboBox(group);
                m_plan->setObjectName(QStringLiteral("flightPlan"));
                for (const orbit::FlightPlan plan : orbit::kAllPlans) {
                    m_plan->addItem(toQString(orbit::planLabel(plan)), static_cast<int>(plan));
                }
                m_plan->setToolTip(
                    tr("Orbit: cut the engine when the apoapsis reaches the target altitude, "
                       "coast up, then burn to make the orbit circular.\n"
                       "Orbit (guided ascent): above 30 km, steer by feedback into the "
                       "target orbit; copes with heavy loads and weak upper stages.\n"
                       "Burn until empty: a pure gravity turn with no second burn.\n"
                       "Lunar flyby / orbit: from that circular parking orbit, a trans-lunar "
                       "injection burn towards the Moon, and for an orbit, braking there.\n"
                       "Custom program: your own list of steps (Flight program panel), "
                       "starting from the plan chosen before."));
                const int row = grid->rowCount();
                grid->addWidget(new QLabel(tr("Flight plan"), group), row, 0);
                grid->addWidget(m_plan, row, 1);
                connect(m_plan, &QComboBox::currentIndexChanged, this, [this](int index) {
                    const auto plan =
                        static_cast<orbit::FlightPlan>(m_plan->itemData(index).toInt());
                    // A new custom program starts as a copy of the plan before.
                    if (plan == orbit::FlightPlan::Custom && m_mission.program.empty()) {
                        m_mission.program = orbit::flightProgramFor(m_mission);
                    }
                    m_mission.plan = plan;
                    refreshWidgets();
                    emit missionChanged(m_mission);
                });
            }
            if (p.group == "Lunar transfer") {
                m_lunarGroup = group;
                buildLunarButtons(grid);
            }
            for (int k = 0; k < orbit::kMaxStages; ++k) {
                if (p.group == "Stage " + std::to_string(k + 1)) {
                    m_stageGroups.at(static_cast<std::size_t>(k)) = group;
                    group->setObjectName(QStringLiteral("stage-group-%1").arg(k + 1));
                }
            }
        }

        auto* label = new QLabel(toQString(p.label), group);
        auto* spinBox = new QDoubleSpinBox(group);
        spinBox->setRange(p.min, p.max);
        spinBox->setSingleStep(p.step);
        spinBox->setDecimals(decimalsFor(p.step));
        spinBox->setSuffix(p.unit.empty() ? QString() : QLatin1Char(' ') + toQString(p.unit));
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
    m_liftoffMass = addRow(tr("Liftoff mass"), tr("Every stage, full, and the payload"));
    m_liftoffThrust = addRow(tr("Liftoff thrust"),
                             tr("The first stage's thrust at sea level: its vacuum thrust less "
                                "the sea-level loss"));
    m_burnTime = addRow(tr("Burn time"),
                        tr("Propellant ÷ mass flow, at full thrust (each stage, bottom first)"));
    m_massFlow = addRow(tr("Mass flow"), tr("F / (Isp · g0)"));
    m_thrustToWeight =
        addRow(tr("Liftoff T/W"), tr("Thrust ÷ weight on the pad. Must be above 1 to lift off."));
    m_idealDeltaV = addRow(tr("Ideal Δv"),
                           tr("Tsiolkovsky, Isp · g0 · ln(m_full / m_empty), for each stage with "
                              "everything above it as its load, added up"));
    m_rotationBoost = addRow(tr("Earth rotation boost"),
                             tr("Speed the launch pad already has along the launch direction"));
    layout->addWidget(group);
}

/// "Aim at the Moon" and "Best launch time": set the launch so that the
/// parking orbit's plane passes through the Moon (lunar_targeting.hpp).
void ParameterPanel::buildLunarButtons(QGridLayout* grid) {
    auto* group = m_lunarGroup;
    m_aimButton = new QPushButton(tr("Aim at the Moon"), group);
    m_aimButton->setObjectName(QStringLiteral("aimAtMoon"));
    m_aimButton->setToolTip(tr("Set the launch direction so that the orbit's plane passes "
                               "through the Moon when the rocket arrives"));
    m_bestTimeButton = new QPushButton(tr("Best launch time"), group);
    m_bestTimeButton->setObjectName(QStringLiteral("bestLaunchTime"));
    m_bestTimeButton->setToolTip(tr("Pick the time of day (on the launch date) when the "
                                    "rocket can head for the Moon flying due east, the "
                                    "cheapest direction, and aim"));
    const int row = grid->rowCount();
    grid->addWidget(m_aimButton, row, 0);
    grid->addWidget(m_bestTimeButton, row, 1);

    const auto aim = [this] {
        const std::optional<double> azimuth = orbit::azimuthToMoon(m_mission);
        if (!azimuth) {
            QMessageBox::information(this, tr("Aim at the Moon"),
                                     tr("The rocket doesn't reach orbit with these settings, "
                                        "so there is nothing to aim yet."));
            return;
        }
        m_mission.guidance.azimuth = *azimuth;
        refreshWidgets();
        emit missionChanged(m_mission);
    };
    connect(m_aimButton, &QPushButton::clicked, this, aim);
    connect(m_bestTimeButton, &QPushButton::clicked, this, [this, aim] {
        orbit::setLaunchHourOfDay(m_mission, orbit::bestLaunchHourForMoon(m_mission));
        aim();
    });
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

        // Parameters only matter for some flight plans, and a swept parameter
        // is set by the sweep, not the slider.
        const bool locked = r.info->key == m_lockedKey;
        // A coast between stages needs more than one stage.
        const bool staged = r.info->key != "stage-coast" || m_mission.rocket.stageCount > 1;
        const bool active = !locked && staged && appliesToPlan(*r.info, m_mission.plan);
        r.label->setEnabled(active);
        r.slider->setEnabled(active);
        r.spinBox->setEnabled(active);
        r.label->setToolTip(locked ? tr("Being swept: see the Sweep panel")
                                   : toQString(r.info->help));
    }
    const QSignalBlocker blockPlan(m_plan);
    m_plan->setCurrentIndex(m_plan->findData(static_cast<int>(m_mission.plan)));
    const bool lunar = m_mission.plan == orbit::FlightPlan::LunarFlyby ||
                       m_mission.plan == orbit::FlightPlan::LunarOrbit;
    for (int k = 1; k < orbit::kMaxStages; ++k) {
        if (QGroupBox* stage = m_stageGroups.at(static_cast<std::size_t>(k))) {
            stage->setVisible(k < m_mission.rocket.stageCount);
        }
    }
    m_aimButton->setEnabled(lunar);
    m_bestTimeButton->setEnabled(lunar);
    const orbit::CalendarTime launch = orbit::calendarFromEpoch(m_mission.launchEpoch);
    const QSignalBlocker blockDate(m_launchDate);
    m_launchDate->setDate(QDate(launch.year, launch.month, launch.day));
    refreshDerived();
}

void ParameterPanel::refreshDerived() {
    const orbit::Rocket& rocket = m_mission.rocket;
    // One value per fitted stage, bottom first: "176 s + 280 s".
    const auto perStage = [&](const auto& format) {
        QStringList parts;
        for (int k = 0; k < rocket.stageCount; ++k) {
            parts.append(format(rocket.stages.at(static_cast<std::size_t>(k)), k));
        }
        return parts.join(QStringLiteral(" + "));
    };
    m_liftoffMass->setText(display::mass(orbit::liftoffMass(rocket)));
    m_liftoffThrust->setText(tr("%1 kN").arg(
        QLocale().toString(orbit::seaLevelThrust(rocket.stages[0]) / 1000.0, 'f', 0)));
    m_burnTime->setText(
        perStage([](const orbit::Stage& s, int) { return display::duration(orbit::burnTime(s)); }));
    m_massFlow->setText(tr("%1 kg/s").arg(perStage([](const orbit::Stage& s, int) {
        return QLocale().toString(orbit::massFlowRate(s), 'f', 1);
    })));

    const double twr = orbit::liftoffThrustToWeight(rocket);
    const QString twrText = QLocale().toString(twr, 'f', 2);
    m_thrustToWeight->setText(
        twr > 1.0 ? twrText
                  : tr("<span style='color:#f87171'>%1 (can't lift off)</span>").arg(twrText));
    QString deltaV = display::speed(orbit::idealDeltaV(rocket));
    if (rocket.stageCount > 1) {
        deltaV += QStringLiteral("<br><span style='color:#8592ad'>%1</span>")
                      .arg(perStage([&](const orbit::Stage&, int k) {
                          return display::speed(orbit::stageDeltaV(rocket, k));
                      }));
    }
    m_idealDeltaV->setText(deltaV);

    // The pad's velocity, projected onto the launch direction.
    const orbit::Vec3 pad = orbit::positionFromLatLon(
        m_mission.site.latitude, m_mission.site.longitude, orbit::kRadiusEarth);
    const orbit::Vec3 heading =
        orbit::horizontalDirection(orbit::localFrame(pad), m_mission.guidance.azimuth);
    const double boost = orbit::dot(orbit::groundVelocity(pad), heading);
    m_rotationBoost->setText((boost >= 0.0 ? QStringLiteral("+") : QString()) +
                             display::speed(boost));
}
