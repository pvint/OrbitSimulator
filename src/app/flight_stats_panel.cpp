// SPDX-License-Identifier: GPL-3.0-or-later
#include "flight_stats_panel.hpp"

#include "display_format.hpp"
#include "orbit/core/astro_time.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include <cmath>

FlightStatsPanel::FlightStatsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* orbitGroup = new QGroupBox(tr("Result"), this);
    auto* form = new QFormLayout(orbitGroup);
    m_resultForm = form;
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
                                 "the surface all the way round. Escape: never comes back.\n"
                                 "A natural body: Flyby, passing by; Temporary capture, bound "
                                 "to the Earth for a while; Impact; or Orbit, bound all along."));
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
    m_pressureLoss =
        addRow(tr("Back-pressure loss"),
               tr("Speed lost because the engine pushes less in thick air, where the air "
                  "presses on its nozzle (the Sea-level loss parameter)"));
    m_pressureLoss->setObjectName(QStringLiteral("pressureLoss"));
    m_duration = addRow(tr("Simulated time"), {});
    layout->addWidget(orbitGroup);

    // Only shown for natural bodies: how they pass the Earth.
    m_passageGroup = new QGroupBox(tr("Passing the Earth"), this);
    m_passageGroup->setObjectName(QStringLiteral("passage"));
    auto* passageForm = new QFormLayout(m_passageGroup);
    const auto addPassageRow = [&](const QString& name, const QString& tooltip) {
        auto* value = new QLabel(m_passageGroup);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setToolTip(tooltip);
        passageForm->addRow(name, value);
        return value;
    };
    m_closest = addPassageRow(tr("Closest approach"),
                              tr("Nearest to the Earth's centre, and the height above the "
                                 "surface (the Earth's radius is 6,371 km)"));
    m_closest->setObjectName(QStringLiteral("closestApproach"));
    m_closestTime = addPassageRow(tr("When"), tr("Time of closest approach (UTC)"));
    m_closestSpeed = addPassageRow(tr("Speed there"), tr("Relative to the Earth's centre"));
    m_aim = addPassageRow(tr("Aimed at"),
                          tr("The impact parameter: how far from the Earth's centre it would "
                             "have passed if the Earth's gravity didn't bend its path. Gravity "
                             "pulls every path in, so the closest approach is nearer."));
    m_aim->setObjectName(QStringLiteral("impactParameter"));
    m_farSpeed = addPassageRow(tr("Speed far away"),
                               tr("v∞, the speed relative to the Earth that is left far from "
                                  "it: on the way in, and on the way out. The Moon and the "
                                  "Sun can change it a little."));
    m_bound = addPassageRow(tr("Bound to the Earth"),
                            tr("How long its energy relative to the Earth was below zero, so "
                               "that, left alone with the Earth, it could not have got away"));
    m_finalOrbit = addPassageRow(tr("Orbit at the end"),
                                 tr("The orbit around the Earth at the end of the time span: a "
                                    "hyperbola (e > 1) leaves for good, an ellipse comes back"));
    layout->addWidget(m_passageGroup);

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

    // Only shown when stages were dropped: where each one went.
    m_stagesGroup = new QGroupBox(tr("Dropped stages"), this);
    auto* stagesLayout = new QVBoxLayout(m_stagesGroup);
    auto* stages = new QLabel(m_stagesGroup);
    stages->setObjectName(QStringLiteral("stages"));
    stages->setTextFormat(Qt::RichText);
    stages->setWordWrap(true);
    stages->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stages->setToolTip(tr("Each empty stage falls back on its own (grey in the 3D view). "
                          "Distances are along the ground from the launch site."));
    stagesLayout->addWidget(stages);
    m_stages = stages;
    layout->addWidget(m_stagesGroup);

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

namespace {

/// A moment of a flight: "T+5 min" for a rocket, a UTC date and time for a
/// natural body.
QString momentOf(const orbit::SimResult& result, double t) {
    if (!result.encounter) return QStringLiteral("T+") + display::duration(t);
    return QString::fromStdString(orbit::formatIsoUtc(result.startEpoch + t))
        .replace(QLatin1Char('T'), QLatin1Char(' '))
        .replace(QLatin1Char('Z'), QStringLiteral(" UTC"));
}

} // namespace

void FlightStatsPanel::showResult(const orbit::SimResult& result) {
    const orbit::OrbitalElements& o = result.orbit;
    const bool body = result.encounter.has_value();
    // A rocket's engine and air read-outs mean nothing for a natural body.
    // (Its orbit is under "Passing the Earth", next to the closest approach,
    // which is what matters for a body.)
    for (QLabel* rocketOnly :
         {m_periapsis, m_apoapsis, m_inclination, m_period, m_eccentricity, m_deltaV,
          m_propellantLeft, m_maxQ, m_dragLoss, m_pressureLoss, m_maxAltitude}) {
        m_resultForm->setRowVisible(rocketOnly, !body);
    }
    m_passageGroup->setVisible(body);
    if (body) {
        const orbit::EncounterSummary& e = *result.encounter;
        const double distance = orbit::norm(e.closest.state.r);
        m_closest->setText(
            QStringLiteral("%1<br><span style='color:#8592ad'>%2</span>")
                .arg(display::distance(distance),
                     distance <= orbit::kRadiusEarth + 1.0
                         ? tr("at the surface")
                         : tr("%1 up").arg(display::distance(distance - orbit::kRadiusEarth))));
        m_closestTime->setText(momentOf(result, e.closest.t));
        m_closestSpeed->setText(display::speed(e.closest.speed()));
        m_aim->setText(std::isfinite(e.impactParameter) ? display::distance(e.impactParameter)
                                                        : QStringLiteral("—"));
        m_farSpeed->setText(
            e.arrivalSpeed > 0.0 || e.departureSpeed > 0.0
                ? tr("%1 in, %2 out")
                      .arg(display::speed(e.arrivalSpeed), display::speed(e.departureSpeed))
                : QStringLiteral("—"));
        m_bound->setText(e.timeBound > 0.0 ? display::duration(e.timeBound) : QStringLiteral("—"));
        const orbit::OrbitalElements& end = e.finalOrbit;
        const QString shape =
            end.bound ? tr("ellipse, %1 × %2, period %3")
                            .arg(display::distance(end.periapsis), display::distance(end.apoapsis),
                                 display::duration(end.period))
                      : tr("hyperbola, e = %1").arg(QLocale().toString(end.eccentricity, 'f', 3));
        m_finalOrbit->setText(
            result.outcome == orbit::Outcome::Impact
                ? QStringLiteral("—")
                : shape + QStringLiteral("<br><span style='color:#8592ad'>%1</span>")
                              .arg(tr("inclination %1").arg(display::angle(end.inclination))));
    }

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
    m_pressureLoss->setText(result.pressureLoss > 0.0 ? display::speed(result.pressureLoss)
                                                      : QStringLiteral("—"));
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
    m_moonGroup->setVisible(lunarPlan || moon.enteredSphereOfInfluence || body);
    m_moonClosest->setText(moon.closestApproachAltitude <= 0.0
                               ? tr("impact")
                               : display::altitude(moon.closestApproachAltitude));
    m_moonClosestTime->setText(momentOf(result, moon.closestApproachTime));
    if (moon.finalOrbit && moon.finalOrbit->bound) {
        m_moonOrbit->setText(display::altitude(moon.finalOrbit->periapsisAltitude) +
                             QStringLiteral(" × ") +
                             display::altitude(moon.finalOrbit->apoapsisAltitude));
        m_moonPeriod->setText(display::duration(moon.finalOrbit->period));
    } else {
        m_moonOrbit->setText(QStringLiteral("—"));
        m_moonPeriod->setText(QStringLiteral("—"));
    }

    m_stagesGroup->setVisible(!result.spentStages.empty());
    QString stages = QStringLiteral("<table cellspacing='2'>");
    for (const orbit::SpentStage& spent : result.spentStages) {
        const orbit::Sample& separation = spent.samples.front();
        const QString fate = spent.impactTime ? tr("came down %1 downrange, T+%2")
                                                    .arg(display::distance(spent.impactDistance),
                                                         display::duration(*spent.impactTime))
                                              : tr("still flying at the end");
        stages += QStringLiteral("<tr><td>%1</td><td>&nbsp;%2</td></tr>"
                                 "<tr><td></td><td style='color:#8592ad'>&nbsp;%3</td></tr>"
                                 "<tr><td></td><td style='color:#8592ad'>&nbsp;%4</td></tr>")
                      .arg(tr("T+%1").arg(display::duration(separation.t)),
                           tr("Stage %1 dropped").arg(spent.stage + 1),
                           tr("at %1, %2")
                               .arg(display::altitude(separation.altitude()),
                                    display::speed(separation.speed())),
                           fate);
    }
    m_stages->setText(stages + QStringLiteral("</table>"));

    if (result.events.empty()) {
        m_events->setText(tr("<i>None: the engine never shut down in flight.</i>"));
        return;
    }
    QString html = QStringLiteral("<table cellspacing='2'>");
    for (const orbit::FlightEvent& e : result.events) {
        // A body's distance is from the Earth's centre, like its closest
        // approach; a rocket's is its altitude.
        const QString where =
            body ? tr("%1 from the centre").arg(display::distance(orbit::norm(e.sample.state.r)))
                 : display::altitude(e.sample.altitude());
        html += QStringLiteral("<tr><td>%1</td><td>&nbsp;%2</td></tr>"
                               "<tr><td></td><td style='color:#8592ad'>&nbsp;%3, %4</td></tr>")
                    .arg(momentOf(result, e.sample.t), QString::fromStdString(e.description), where,
                         display::speed(e.sample.speed()));
    }
    html += QStringLiteral("</table>");
    m_events->setText(html);
}
