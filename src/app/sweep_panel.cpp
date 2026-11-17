// SPDX-License-Identifier: GPL-3.0-or-later
#include "sweep_panel.hpp"

#include "display_format.hpp"
#include "orbit/core/mission_parameters.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

int decimalsFor(double step) {
    if (step >= 1.0) return 0;
    if (step >= 0.1) return 1;
    return 2;
}

/// Altitude in km without the unit (the column header has it).
QString km(double metres) {
    if (!std::isfinite(metres)) return QStringLiteral("∞");
    return QLocale().toString(metres / 1000.0, 'f', 0);
}

// Legend columns.
constexpr int ColorColumn = 0;
constexpr int ValueColumn = 1;
constexpr int OutcomeColumn = 2;
constexpr int OrbitColumn = 3;
constexpr int DeltaVColumn = 4;
constexpr int ColumnCount = 5;

} // namespace

SweepPanel::SweepPanel(QWidget* parent)
    : QWidget(parent), m_mission(orbit::defaultMission()), m_parameter(new QComboBox(this)),
      m_from(new QDoubleSpinBox(this)), m_to(new QDoubleSpinBox(this)), m_steps(new QSpinBox(this)),
      m_run(new QPushButton(tr("Run sweep"), this)),
      m_single(new QPushButton(tr("Single flight"), this)), m_hint(new QLabel(this)),
      m_legend(new QTableWidget(0, ColumnCount, this)) {
    auto* layout = new QVBoxLayout(this);

    for (const orbit::ParameterInfo& p : orbit::missionParameters()) {
        m_parameter->addItem(QStringLiteral("%1 (%2)").arg(toQString(p.label), toQString(p.unit)),
                             toQString(p.key));
    }
    m_parameter->setCurrentIndex(m_parameter->findData(QStringLiteral("kick-angle")));
    m_parameter->setObjectName(QStringLiteral("sweepParameter"));
    m_from->setObjectName(QStringLiteral("sweepFrom"));
    m_to->setObjectName(QStringLiteral("sweepTo"));
    m_steps->setObjectName(QStringLiteral("sweepSteps"));
    m_run->setObjectName(QStringLiteral("runSweep"));
    m_legend->setObjectName(QStringLiteral("sweepLegend"));

    m_steps->setRange(2, 60);
    m_steps->setValue(9);
    m_steps->setToolTip(tr("Number of flights, including both ends of the range"));

    auto* form = new QFormLayout;
    form->addRow(tr("Parameter"), m_parameter);
    auto* range = new QHBoxLayout;
    range->addWidget(m_from, 1);
    range->addWidget(new QLabel(QStringLiteral("→"), this));
    range->addWidget(m_to, 1);
    form->addRow(tr("Range"), range);
    form->addRow(tr("Flights"), m_steps);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_run);
    buttons->addWidget(m_single);
    layout->addLayout(buttons);
    m_single->setToolTip(tr("Back to one flight, with the parameters as set"));

    m_hint->setWordWrap(true);
    m_hint->setText(tr("Flies the mission once for each value, all at once. "
                       "Click a row to look at that flight."));
    m_hint->setStyleSheet(QStringLiteral("color: #8592ad"));
    layout->addWidget(m_hint);

    m_legend->setHorizontalHeaderLabels(
        {QString(), tr("Value"), tr("Outcome"), tr("Orbit (km)"), tr("Δv (m/s)")});
    m_legend->verticalHeader()->hide();
    m_legend->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_legend->setSelectionMode(QAbstractItemView::SingleSelection);
    m_legend->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_legend->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_legend->horizontalHeader()->setSectionResizeMode(ColorColumn, QHeaderView::Fixed);
    m_legend->horizontalHeader()->setMinimumSectionSize(12);
    m_legend->horizontalHeader()->setStretchLastSection(true);
    m_legend->setColumnWidth(ColorColumn, 14);
    layout->addWidget(m_legend, 1);

    connect(m_parameter, &QComboBox::currentIndexChanged, this, &SweepPanel::suggestRange);
    connect(m_run, &QPushButton::clicked, this, [this] { emit sweepRequested(currentSpec()); });
    connect(m_single, &QPushButton::clicked, this, &SweepPanel::singleFlightRequested);
    connect(m_legend, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (row >= 0) emit runSelected(row);
    });

    suggestRange();
}

void SweepPanel::setMission(const orbit::Mission& mission) { m_mission = mission; }

/// Set the spin boxes up for the chosen parameter, with a range of ±50 %
/// around its current value (or its full range, if it is zero).
void SweepPanel::suggestRange() {
    const orbit::ParameterInfo* p =
        orbit::findParameter(m_parameter->currentData().toString().toStdString());
    if (p == nullptr) return;
    for (QDoubleSpinBox* box : {m_from, m_to}) {
        box->setRange(p->min, p->max);
        box->setDecimals(decimalsFor(p->step));
        box->setSingleStep(p->step);
        box->setSuffix(QLatin1Char(' ') + toQString(p->unit));
    }
    const double value = p->get(m_mission);
    if (std::abs(value) < p->step) {
        m_from->setValue(p->min);
        m_to->setValue(p->max);
    } else {
        m_from->setValue(std::clamp(value * 0.5, p->min, p->max));
        m_to->setValue(std::clamp(value * 1.5, p->min, p->max));
    }
}

orbit::SweepSpec SweepPanel::currentSpec() const {
    return {m_parameter->currentData().toString().toStdString(), m_from->value(), m_to->value(),
            m_steps->value()};
}

void SweepPanel::setSpec(const orbit::SweepSpec& spec) {
    const int index = m_parameter->findData(QString::fromStdString(spec.parameter));
    if (index < 0) return;
    m_parameter->setCurrentIndex(index); // sets up the spin boxes for this parameter
    m_from->setValue(spec.from);
    m_to->setValue(spec.to);
    m_steps->setValue(spec.steps);
}

void SweepPanel::showRunSet(const RunSet& runs, int selected) {
    const QSignalBlocker block(m_legend); // don't report our own selection change
    if (!runs.sweep) {
        m_legend->setRowCount(0);
        return;
    }
    const orbit::ParameterInfo* p = orbit::findParameter(runs.sweep->parameter);
    const int decimals = p ? decimalsFor(p->step) + 1 : 2;
    const QString unit = p ? toQString(p->unit) : QString();

    m_legend->setRowCount(static_cast<int>(runs.runs.size()));
    for (int row = 0; row < m_legend->rowCount(); ++row) {
        const FlightRun& run = runs.runs[static_cast<std::size_t>(row)];
        const orbit::SimResult& r = *run.result;
        const auto setCell = [&](int column, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            if (column != OutcomeColumn) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_legend->setItem(row, column, item);
            return item;
        };
        setCell(ColorColumn, QString())->setBackground(run.color);
        setCell(ValueColumn,
                QLocale().toString(run.value, 'f', decimals) + QLatin1Char(' ') + unit);
        setCell(OutcomeColumn, display::outcomeLabel(r.outcome))
            ->setForeground(display::outcomeColor(r.outcome));
        const bool hasOrbit =
            r.outcome == orbit::Outcome::Orbit || r.outcome == orbit::Outcome::Escape;
        // Periapsis × apoapsis altitude for orbits, otherwise the highest point.
        setCell(OrbitColumn, hasOrbit ? km(r.orbit.periapsisAltitude) + QStringLiteral(" × ") +
                                            km(r.orbit.apoapsisAltitude)
                                      : tr("max %1").arg(km(r.maxAltitude)));
        setCell(DeltaVColumn, QLocale().toString(r.deltaVUsed, 'f', 0));
    }
    if (selected >= 0 && selected < m_legend->rowCount()) m_legend->selectRow(selected);
}
