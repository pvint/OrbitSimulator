// SPDX-License-Identifier: GPL-3.0-or-later
#include "program_panel.hpp"

#include "orbit/core/flight_program.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

// Table columns.
constexpr int NameColumn = 0;
constexpr int EngineColumn = 1;
constexpr int SteeringColumn = 2;
constexpr int EndColumn = 3;
constexpr int ValueColumn = 4;
constexpr int TimeoutColumn = 5;

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// A starting value when a step switches to this end condition (SI units).
double defaultValue(orbit::StepEnd end) {
    const std::string_view unit = orbit::stepEndUnit(end);
    if (unit == "km") return 200e3;
    if (unit == "rev") return 1.0;
    if (unit == "s") return 60.0;
    return 0.0;
}

/// Spin box range and precision for a step value, in display units.
void setUpValueBox(QDoubleSpinBox* box, orbit::StepEnd end) {
    const std::string_view unit = orbit::stepEndUnit(end);
    box->setSuffix(unit.empty() ? QString() : QLatin1Char(' ') + toQString(unit));
    if (unit == "km") {
        box->setRange(-6000.0, 1e7);
        box->setDecimals(1);
        box->setSingleStep(10.0);
    } else if (unit == "°") {
        box->setRange(-360.0, 360.0);
        box->setDecimals(2);
        box->setSingleStep(1.0);
    } else if (unit == "rev") {
        box->setRange(0.0, 1000.0);
        box->setDecimals(2);
        box->setSingleStep(0.5);
    } else {
        box->setRange(0.0, 1e8);
        box->setDecimals(0);
        box->setSingleStep(10.0);
    }
}

} // namespace

ProgramPanel::ProgramPanel(QWidget* parent)
    : QWidget(parent), m_mission(orbit::defaultMission()), m_hint(new QLabel(this)),
      m_table(new QTableWidget(0, 6, this)), m_customize(new QPushButton(tr("Customize"), this)),
      m_add(new QPushButton(tr("Add step"), this)), m_remove(new QPushButton(tr("Remove"), this)),
      m_up(new QPushButton(tr("Move up"), this)), m_down(new QPushButton(tr("Move down"), this)) {
    auto* layout = new QVBoxLayout(this);

    auto* buttons = new QHBoxLayout;
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: #8592ad"));
    buttons->addWidget(m_hint, 1);
    for (QPushButton* b : {m_customize, m_add, m_remove, m_up, m_down}) buttons->addWidget(b);
    layout->addLayout(buttons);

    m_customize->setObjectName(QStringLiteral("customizeProgram"));
    m_customize->setToolTip(tr("Copy this plan's steps into a custom program you can edit"));
    m_add->setObjectName(QStringLiteral("addStep"));
    m_add->setToolTip(tr("Insert a step after the selected one"));
    m_remove->setObjectName(QStringLiteral("removeStep"));
    m_table->setObjectName(QStringLiteral("programTable"));

    m_table->setHorizontalHeaderLabels(
        {tr("Step"), tr("Engine"), tr("Steering"), tr("Until"), tr("Value"), tr("Timeout")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeaderItem(TimeoutColumn)
        ->setToolTip(tr("The step ends after this long even if its condition isn't met"));
    m_table->horizontalHeaderItem(EndColumn)->setToolTip(
        tr("The step ends when this happens. \"Reaches\" works both ways: raising "
           "or lowering, whichever is needed"));
    layout->addWidget(m_table, 1);

    connect(m_customize, &QPushButton::clicked, this, &ProgramPanel::customize);
    connect(m_add, &QPushButton::clicked, this, &ProgramPanel::addStep);
    connect(m_remove, &QPushButton::clicked, this, &ProgramPanel::removeStep);
    connect(m_up, &QPushButton::clicked, this, [this] { moveStep(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { moveStep(+1); });

    rebuild();
}

void ProgramPanel::setMission(const orbit::Mission& mission) {
    const bool wasCustom = m_mission.plan == orbit::FlightPlan::Custom;
    m_mission = mission;
    const bool custom = mission.plan == orbit::FlightPlan::Custom;
    if (custom == wasCustom && orbit::flightProgramFor(mission) == m_shown) return;
    rebuild();
}

void ProgramPanel::setLockedKey(std::string_view key) {
    m_lockedKey = key;
    rebuild();
}

/// Fill the table from the mission's program.
void ProgramPanel::rebuild() {
    const bool editable = m_mission.plan == orbit::FlightPlan::Custom;
    m_shown = orbit::flightProgramFor(m_mission);
    const int selected = m_table->currentRow();

    m_table->setRowCount(0);
    m_table->setRowCount(static_cast<int>(m_shown.size()));
    for (std::size_t i = 0; i < m_shown.size(); ++i) {
        addRow(static_cast<int>(i), m_shown[i], editable);
    }
    if (selected >= 0 && selected < m_table->rowCount()) {
        m_table->setCurrentCell(selected, NameColumn);
    }

    m_customize->setVisible(!editable);
    for (QPushButton* b : {m_add, m_remove, m_up, m_down}) b->setVisible(editable);
    m_hint->setText(editable
                        ? tr("Steps run in order; each ends when its condition is met, or at its "
                             "timeout. Step values can be swept from the Sweep panel.")
                        : tr("The steps the “%1” plan makes. Customize to edit them.")
                              .arg(toQString(orbit::planLabel(m_mission.plan))));
}

void ProgramPanel::addRow(int row, const orbit::FlightStep& step, bool editable) {
    const auto index = static_cast<std::size_t>(row);
    // Every edit selects its row, so Remove / Move act on the step last touched.
    const auto touch = [this, row] { m_table->setCurrentCell(row, NameColumn); };

    auto* name = new QLineEdit(QString::fromStdString(step.name));
    connect(name, &QLineEdit::editingFinished, this, [this, index, name, touch] {
        touch();
        if (m_mission.program.at(index).name == name->text().toStdString()) return;
        m_mission.program.at(index).name = name->text().toStdString();
        changed();
    });

    auto* engine = new QCheckBox(tr("Burn"));
    engine->setChecked(step.engineOn);
    engine->setToolTip(tr("Checked: the engine fires. Unchecked: coast"));
    connect(engine, &QCheckBox::toggled, this, [this, index, touch](bool on) {
        touch();
        m_mission.program.at(index).engineOn = on;
        changed();
        QMetaObject::invokeMethod(this, &ProgramPanel::rebuild, Qt::QueuedConnection);
    });

    // Combo boxes sized to a moderate length, not their longest entry, so
    // the whole table fits; the full text shows when they open.
    const auto compact = [](QComboBox* box) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(16);
    };
    auto* steering = new QComboBox;
    compact(steering);
    for (const orbit::Steering s : orbit::kAllSteerings) {
        steering->addItem(toQString(orbit::steeringLabel(s)), static_cast<int>(s));
    }
    steering->setCurrentIndex(steering->findData(static_cast<int>(step.steering)));
    steering->setEnabled(editable && step.engineOn); // a coast doesn't steer
    connect(steering, &QComboBox::currentIndexChanged, this, [this, index, steering, touch] {
        touch();
        m_mission.program.at(index).steering =
            static_cast<orbit::Steering>(steering->currentData().toInt());
        changed();
    });

    auto* end = new QComboBox;
    compact(end);
    for (const orbit::StepEnd e : orbit::kAllStepEnds) {
        end->addItem(toQString(orbit::stepEndLabel(e)), static_cast<int>(e));
        end->setItemData(end->count() - 1, toQString(orbit::stepEndLabel(e)), Qt::ToolTipRole);
    }
    end->setToolTip(end->currentText());
    end->setCurrentIndex(end->findData(static_cast<int>(step.end)));
    connect(end, &QComboBox::currentIndexChanged, this, [this, index, end, touch] {
        touch();
        orbit::FlightStep& s = m_mission.program.at(index);
        s.end = static_cast<orbit::StepEnd>(end->currentData().toInt());
        s.value = defaultValue(s.end);
        changed();
        // The value's unit changed: rebuild once this signal is over.
        QMetaObject::invokeMethod(this, &ProgramPanel::rebuild, Qt::QueuedConnection);
    });

    auto* value = new QDoubleSpinBox;
    value->setKeyboardTracking(false);
    setUpValueBox(value, step.end);
    value->setValue(orbit::stepValueToDisplay(step.end, step.value));
    const bool locked = m_lockedKey == "step-" + std::to_string(row + 1) + "-value";
    value->setToolTip(locked ? tr("Being swept: see the Sweep panel") : QString());
    connect(value, &QDoubleSpinBox::valueChanged, this, [this, index, touch](double v) {
        touch();
        orbit::FlightStep& s = m_mission.program.at(index);
        s.value = orbit::stepValueFromDisplay(s.end, v);
        changed();
    });

    auto* timeout = new QDoubleSpinBox;
    timeout->setKeyboardTracking(false);
    timeout->setRange(0.0, 1e8);
    timeout->setDecimals(0);
    timeout->setSingleStep(60.0);
    timeout->setSuffix(QStringLiteral(" s"));
    timeout->setSpecialValueText(tr("none")); // 0 = no time limit
    timeout->setValue(std::isfinite(step.timeout) ? step.timeout : 0.0);
    connect(timeout, &QDoubleSpinBox::valueChanged, this, [this, index, touch](double v) {
        touch();
        m_mission.program.at(index).timeout = v > 0.0 ? v : std::numeric_limits<double>::infinity();
        changed();
    });

    for (QWidget* w :
         {static_cast<QWidget*>(name), static_cast<QWidget*>(engine), static_cast<QWidget*>(end),
          static_cast<QWidget*>(value), static_cast<QWidget*>(timeout)}) {
        w->setEnabled(editable && (w != value || !locked));
    }
    value->setObjectName(QStringLiteral("stepValue-%1").arg(row + 1));
    m_table->setCellWidget(row, NameColumn, name);
    m_table->setCellWidget(row, EngineColumn, engine);
    m_table->setCellWidget(row, SteeringColumn, steering);
    m_table->setCellWidget(row, EndColumn, end);
    if (orbit::stepEndHasValue(step.end)) {
        m_table->setCellWidget(row, ValueColumn, value);
    } else {
        // "At apoapsis" and the like need no number.
        delete value;
        auto* none = new QLabel(QStringLiteral("—"));
        none->setAlignment(Qt::AlignCenter);
        none->setEnabled(false);
        m_table->setCellWidget(row, ValueColumn, none);
    }
    m_table->setCellWidget(row, TimeoutColumn, timeout);
}

void ProgramPanel::changed() {
    m_shown = m_mission.program;
    emit missionChanged(m_mission);
}

/// Turn the plan's steps into a custom program, ready to edit.
void ProgramPanel::customize() {
    m_mission.program = orbit::flightProgramFor(m_mission);
    m_mission.plan = orbit::FlightPlan::Custom;
    rebuild();
    changed();
}

int ProgramPanel::currentStep() const { return m_table->currentRow(); }

void ProgramPanel::addStep() {
    const int row = currentStep() + 1; // after the selected step, or first
    orbit::FlightStep step{
        .name = tr("New step").toStdString(), .end = orbit::StepEnd::Duration, .value = 60.0};
    m_mission.program.insert(m_mission.program.begin() + row, step);
    rebuild();
    m_table->setCurrentCell(row, NameColumn);
    changed();
}

void ProgramPanel::removeStep() {
    const int row = currentStep();
    if (row < 0 || std::cmp_greater_equal(row, m_mission.program.size())) return;
    m_mission.program.erase(m_mission.program.begin() + row);
    rebuild();
    m_table->setCurrentCell(std::min(row, m_table->rowCount() - 1), NameColumn);
    changed();
}

void ProgramPanel::moveStep(int offset) {
    const int row = currentStep();
    const int target = row + offset;
    if (row < 0 || target < 0 || std::cmp_greater_equal(target, m_mission.program.size())) return;
    std::swap(m_mission.program.at(static_cast<std::size_t>(row)),
              m_mission.program.at(static_cast<std::size_t>(target)));
    rebuild();
    m_table->setCurrentCell(target, NameColumn);
    changed();
}
