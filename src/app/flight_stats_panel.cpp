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
        addRow(tr("Periapsis"), tr("Lowest point of the orbit after the last engine shutdown"));
    m_apoapsis = addRow(tr("Apoapsis"), tr("Highest point of the orbit"));
    m_inclination = addRow(tr("Inclination"),
                           tr("Tilt of the orbit against the equator. 0° = eastward along the "
                              "equator, 90° = over the poles, over 90° = westward (retrograde)."));
    m_period = addRow(tr("Period"), tr("Time for one full orbit"));
    m_eccentricity =
        addRow(tr("Eccentricity"), tr("0 = circle, below 1 = ellipse, 1 or more = escape"));
    m_maxAltitude = addRow(tr("Max altitude"), tr("Highest point reached during the simulation"));
    m_deltaV = addRow(tr("Δv used"), tr("Isp · g0 · ln(m0 / m_final)"));
    m_propellantLeft = addRow(tr("Propellant left"), {});
    m_duration = addRow(tr("Simulated time"), {});
    layout->addWidget(orbitGroup);

    auto* eventsGroup = new QGroupBox(tr("Engine events"), this);
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
    m_duration->setText(display::duration(result.duration));

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
