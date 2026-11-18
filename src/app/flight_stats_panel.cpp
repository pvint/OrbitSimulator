// SPDX-License-Identifier: GPL-3.0-or-later
#include "flight_stats_panel.hpp"

#include "display_format.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

FlightStatsPanel::FlightStatsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* orbitGroup = new QGroupBox(tr("Result"), this);
    auto* form = new QFormLayout(orbitGroup);
    const auto addRow = [&](const QString& name, const QString& tooltip) {
        auto* value = new QLabel(orbitGroup);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setToolTip(tooltip);
        form->addRow(name, value);
        return value;
    };
    m_outcome =
        addRow(tr("Outcome"), tr("Crashed: never reached 100 km. Suborbital: reached "
                                 "space but the orbit dips underground. Orbit: clears "
                                 "the surface all the way round. Escape: never comes back."));
    m_outcome->setObjectName(QStringLiteral("outcome"));
    m_periapsis =
        addRow(tr("Periapsis"),
               tr("Lowest point of the orbit round the Earth (after the last engine shutdown)"));
    m_apoapsis = addRow(tr("Apoapsis"), tr("Highest point of the orbit round the Earth"));
    m_inclination = addRow(tr("Inclination"),
                           tr("Tilt of the orbit against the equator. 0° = eastward along the "
                              "equator, 90° = over the poles, over 90° = westward (retrograde)."));
    m_period = addRow(tr("Period"), tr("Time for one full orbit"));
    m_eccentricity =
        addRow(tr("Eccentricity"), tr("0 = circle, below 1 = ellipse, 1 or more = escape"));
    m_maxAltitude = addRow(tr("Max altitude"), tr("Highest point reached during the simulation"));
    m_deltaV = addRow(tr("Δv used"), tr("Isp · g0 · ln(m0 / m_final)"));
    m_propellantLeft = addRow(tr("Propellant left"), {});
    m_maxQ =
        addRow(tr("Max Q"), tr("Highest dynamic pressure on the way up, q = ½ρv²: the push of the "
                               "oncoming air, and the hardest moment for the rocket's structure"));
    m_maxQ->setObjectName(QStringLiteral("maxQ"));
    m_dragLoss = addRow(tr("Drag loss"), tr("Speed taken away by air resistance on the way up: "
                                            "Δv the engine spent pushing through the air"));
    m_dragLoss->setObjectName(QStringLiteral("dragLoss"));
    m_duration = addRow(tr("Simulated time"), {});
    layout->addWidget(orbitGroup);

    // Only shown for flights that head for the Moon.
    m_moonGroup = new QGroupBox(tr("Moon"), this);
    auto* moonForm = new QFormLayout(m_moonGroup);
    const auto addMoonRow = [&](const QString& name, const QString& tooltip) {
        auto* value = new QLabel(m_moonGroup);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setToolTip(tooltip);
        moonForm->addRow(name, value);
        return value;
    };
    m_moonClosest =
        addMoonRow(tr("Closest approach"), tr("Lowest height above the Moon's surface"));
    m_moonClosest->setObjectName(QStringLiteral("moonClosest"));
    m_moonClosestTime = addMoonRow(tr("When"), tr("Time after liftoff"));
    m_moonOrbit = addMoonRow(tr("Orbit round the Moon"),
                             tr("Periapsis × apoapsis above the Moon's surface, at the end"));
    m_moonPeriod = addMoonRow(tr("Period"), tr("Time for one orbit of the Moon"));
    layout->addWidget(m_moonGroup);

    auto* eventsGroup = new QGroupBox(tr("Events"), this);
    auto* eventsLayout = new QVBoxLayout(eventsGroup);
    m_events = new QLabel(eventsGroup);
    m_events->setTextFormat(Qt::RichText);
    m_events->setWordWrap(true);
    m_events->setTextInteractionFlags(Qt::TextSelectableByMouse);
    eventsLayout->addWidget(m_events);
    layout->addWidget(eventsGroup);
    layout->addStretch(1);
}

void FlightStatsPanel::showResult(const orbit::SimResult& result) {
    const orbit::OrbitalElements& o = result.orbit;

    m_outcome->setText(QStringLiteral("<b style='color:%1'>%2</b>")
                           .arg(display::outcomeColor(result.outcome).name(),
                                display::outcomeLabel(result.outcome)));
    const QString underground = o.periapsisAltitude < 0.0 ? tr(" (underground)") : QString();
    m_periapsis->setText(display::altitude(o.periapsisAltitude) + underground);
    m_apoapsis->setText(display::altitude(o.apoapsisAltitude));
    m_inclination->setText(display::angle(o.inclination));
    m_period->setText(o.bound ? display::duration(o.period) : QStringLiteral("—"));
    m_eccentricity->setText(QLocale().toString(o.eccentricity, 'f', 4));
    m_maxAltitude->setText(display::altitude(result.maxAltitude));
    m_deltaV->setText(display::speed(result.deltaVUsed));
    m_propellantLeft->setText(display::mass(result.propellantLeft));
    if (result.maxDynamicPressure) {
        const orbit::Sample& s = *result.maxDynamicPressure;
        const orbit::Aerodynamics aero = orbit::aerodynamicsAt(result, s);
        m_maxQ->setText(QStringLiteral("%1<br><span style='color:#8592ad'>T+%2, %3, Mach %4</span>")
                            .arg(display::pressure(aero.dynamicPressure), display::duration(s.t),
                                 display::altitude(s.altitude()),
                                 QLocale().toString(aero.mach, 'f', 1)));
        m_dragLoss->setText(display::speed(result.dragLoss));
    } else {
        m_maxQ->setText(QStringLiteral("—"));
        m_dragLoss->setText(QStringLiteral("—"));
    }
    m_duration->setText(display::duration(result.duration));

    const bool lunarPlan = result.mission.plan == orbit::FlightPlan::LunarFlyby ||
                           result.mission.plan == orbit::FlightPlan::LunarOrbit;
    const orbit::LunarSummary& moon = result.lunar;
    m_moonGroup->setVisible(lunarPlan || moon.enteredSphereOfInfluence);
    m_moonClosest->setText(moon.closestApproachAltitude <= 0.0
                               ? tr("impact")
                               : display::altitude(moon.closestApproachAltitude));
    m_moonClosestTime->setText(QStringLiteral("T+") + display::duration(moon.closestApproachTime));
    if (moon.finalOrbit && moon.finalOrbit->bound) {
        m_moonOrbit->setText(display::altitude(moon.finalOrbit->periapsisAltitude) +
                             QStringLiteral(" × ") +
                             display::altitude(moon.finalOrbit->apoapsisAltitude));
        m_moonPeriod->setText(display::duration(moon.finalOrbit->period));
    } else {
        m_moonOrbit->setText(QStringLiteral("—"));
        m_moonPeriod->setText(QStringLiteral("—"));
    }

    if (result.events.empty()) {
        m_events->setText(tr("<i>None: the engine never shut down in flight.</i>"));
        return;
    }
    QString html = QStringLiteral("<table cellspacing='2'>");
    for (const orbit::FlightEvent& e : result.events) {
        html += QStringLiteral("<tr><td>T+%1</td><td>&nbsp;%2</td></tr>"
                               "<tr><td></td><td style='color:#8592ad'>&nbsp;%3, %4</td></tr>")
                    .arg(display::duration(e.sample.t), QString::fromStdString(e.description),
                         display::altitude(e.sample.altitude()), display::speed(e.sample.speed()));
    }
    html += QStringLiteral("</table>");
    m_events->setText(html);
}
