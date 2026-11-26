// SPDX-License-Identifier: GPL-3.0-or-later
#include "burn_planner.hpp"

#include "display_format.hpp"
#include "game_session.hpp"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kDay = 86400.0;
/// The "when" slider reaches this far ahead, in tenths of a day.
constexpr int kWhenSliderDays = 1100;
/// The direction sliders reach ±this (m/s); the spin boxes go further.
constexpr int kAxisSliderRange = 6000;
constexpr double kAxisSpinRange = 30000.0;

} // namespace

BurnPlanner::BurnPlanner(GameSession* session, QWidget* parent)
    : QWidget(parent), m_session(session), m_fuel(new QProgressBar(this)),
      m_fuelText(new QLabel(this)), m_list(new QListWidget(this)),
      m_whenSlider(new QSlider(Qt::Horizontal, this)), m_whenSpin(new QDoubleSpinBox(this)),
      m_whenDate(new QDateTimeEdit(this)), m_size(new QLabel(this)) {
    auto* layout = new QVBoxLayout(this);

    // --- Fuel ----------------------------------------------------------
    auto* fuelTitle = new QLabel(tr("<b>Fuel</b>"), this);
    m_fuel->setObjectName(QStringLiteral("bar-fuel"));
    m_fuel->setRange(0, 1000);
    m_fuel->setTextVisible(false);
    m_fuel->setToolTip(tr("How much speed change (Δv) your propellant can still buy, after the "
                          "burns you have planned."));
    m_fuelText->setWordWrap(true);
    layout->addWidget(fuelTitle);
    layout->addWidget(m_fuel);
    layout->addWidget(m_fuelText);
    layout->addSpacing(8);

    // --- Burns ---------------------------------------------------------
    layout->addWidget(new QLabel(tr("<b>Burns</b>"), this));
    m_list->setObjectName(QStringLiteral("list-burns"));
    m_list->setMaximumHeight(110);
    layout->addWidget(m_list);
    auto* buttons = new QHBoxLayout;
    m_add = new QPushButton(tr("Add burn"), this);
    m_add->setObjectName(QStringLiteral("button-add-burn"));
    m_add->setToolTip(tr("Plan a burn: pick when, then how hard in each direction, and watch "
                         "the path change."));
    m_remove = new QPushButton(tr("Remove"), this);
    m_remove->setObjectName(QStringLiteral("button-remove-burn"));
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    layout->addLayout(buttons);

    // --- The selected burn ----------------------------------------------
    m_editor = new QGroupBox(tr("Selected burn"), this);
    m_editor->setObjectName(QStringLiteral("group-burn"));
    auto* form = new QFormLayout(m_editor);
    auto* whenRow = new QHBoxLayout;
    m_whenSpin->setObjectName(QStringLiteral("spin-burn-days"));
    m_whenSpin->setRange(0.0, 20000.0); // trips from Neptune take decades
    m_whenSpin->setDecimals(1);
    m_whenSpin->setSuffix(tr(" days"));
    m_whenSpin->setPrefix(tr("in "));
    whenRow->addWidget(m_whenSpin);
    whenRow->addStretch(1);
    m_whenDate->setObjectName(QStringLiteral("edit-burn-date"));
    m_whenDate->setCalendarPopup(true);
    m_whenDate->setTimeZone(QTimeZone::UTC);
    m_whenDate->setDisplayFormat(QStringLiteral("d MMM yyyy  HH:mm 'UTC'"));
    m_whenDate->setToolTip(tr("Type a date and time, or open the calendar to pick one."));
    m_whenSlider->setObjectName(QStringLiteral("slider-burn-days"));
    m_whenSlider->setRange(0, kWhenSliderDays * 10);
    form->addRow(tr("When"), whenRow);
    form->addRow(tr("Date"), m_whenDate);
    form->addRow(QString(), m_whenSlider);
    m_prograde = addAxis(form, tr("Prograde"), QStringLiteral("prograde"),
                         tr("Along your motion: faster, which swings the far side of your orbit "
                            "outwards. Negative: retrograde, slower, and it swings inwards."));
    m_radial = addAxis(form, tr("Outward"), QStringLiteral("radial"),
                       tr("Square to your motion, away from the Sun (or the Earth, near it): "
                          "turns your path outwards. Negative: inwards."));
    m_normal = addAxis(form, tr("North"), QStringLiteral("normal"),
                       tr("Out of your orbit's plane, to its north: tilts the orbit. Negative: "
                          "south."));
    m_size->setWordWrap(true);
    form->addRow(m_size);
    m_aim = new QPushButton(tr("Aim at a planet…"), m_editor);
    m_aim->setObjectName(QStringLiteral("button-aim"));
    m_aim->setToolTip(tr("Change this burn so your path passes a planet where you choose: how "
                         "close, and on which side. A pass bends your path and can change "
                         "your speed around the Sun for free: a gravity assist."));
    form->addRow(m_aim);
    layout->addWidget(m_editor);

    m_hint = new QPushButton(tr("Hint"), this);
    m_hint->setObjectName(QStringLiteral("button-hint"));
    m_hint->setToolTip(tr("Stuck? Plans the next step for you: the way home, a course "
                          "correction, or braking to stay."));
    layout->addWidget(m_hint);
    layout->addStretch(1);

    connect(m_add, &QPushButton::clicked, this, [this] {
        // A new burn a tenth of the way along what's left of the path,
        // or soon, if the flight is short.
        double when = m_session->now() + kDay;
        if (const auto* p = m_session->prediction()) {
            when = std::max(when, m_session->now() + 0.1 * (p->endEpoch - m_session->now()));
        }
        m_session->addBurn(when);
    });
    connect(m_remove, &QPushButton::clicked, this,
            [this] { m_session->removeBurn(m_session->selected()); });
    connect(m_hint, &QPushButton::clicked, m_session, &GameSession::requestHint);
    connect(m_aim, &QPushButton::clicked, this, &BurnPlanner::openAimDialog);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating) m_session->select(row);
    });

    connect(m_whenSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;
        const QSignalBlocker block(m_whenSpin);
        m_whenSpin->setValue(value / 10.0);
        editorChanged();
    });
    connect(m_whenSpin, &QDoubleSpinBox::valueChanged, this, [this] { editorChanged(); });
    connect(m_whenDate, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime& when) {
        if (m_updating) return;
        const int index = m_session->selected();
        if (index < 0) return;
        // The date picks the moment exactly; the spin box and slider follow
        // when the session reports the change.
        orbit::Burn burn = m_session->burns()[static_cast<std::size_t>(index)];
        burn.epoch = display::epochFromDateTime(when);
        m_session->setBurn(index, burn);
    });

    connect(m_session, &GameSession::burnsChanged, this, &BurnPlanner::refresh);
    connect(m_session, &GameSession::selectionChanged, this, &BurnPlanner::refresh);
    connect(m_session, &GameSession::nowChanged, this, &BurnPlanner::refresh);
    connect(m_session, &GameSession::puzzleChanged, this, &BurnPlanner::refresh);
    connect(m_session, &GameSession::predictionChanged, this, &BurnPlanner::refreshFuel);
    connect(m_session, &GameSession::busyChanged, this, [this](bool busy) {
        m_hint->setEnabled(!busy && m_session->hasPuzzle());
        m_aim->setEnabled(!busy);
    });
    refresh();
}

BurnPlanner::Axis BurnPlanner::addAxis(QFormLayout* form, const QString& label, const QString& key,
                                       const QString& help) {
    Axis axis;
    axis.spin = new QDoubleSpinBox(m_editor);
    axis.spin->setObjectName(QStringLiteral("spin-burn-") + key);
    axis.spin->setRange(-kAxisSpinRange, kAxisSpinRange);
    axis.spin->setDecimals(1);
    axis.spin->setSuffix(tr(" m/s"));
    axis.spin->setAccelerated(true);
    axis.spin->setToolTip(help);
    axis.slider = new QSlider(Qt::Horizontal, m_editor);
    axis.slider->setObjectName(QStringLiteral("slider-burn-") + key);
    axis.slider->setRange(-kAxisSliderRange, kAxisSliderRange);
    axis.slider->setToolTip(help);
    auto* name = new QLabel(label, m_editor);
    name->setToolTip(help);
    form->addRow(name, axis.spin);
    form->addRow(QString(), axis.slider);

    const Axis copy = axis;
    connect(axis.slider, &QSlider::valueChanged, this, [this, copy](int value) {
        if (m_updating) return;
        const QSignalBlocker block(copy.spin);
        copy.spin->setValue(value);
        editorChanged();
    });
    connect(axis.spin, &QDoubleSpinBox::valueChanged, this, [this, copy](double value) {
        if (m_updating) return;
        const QSignalBlocker block(copy.slider);
        copy.slider->setValue(static_cast<int>(std::lround(value)));
        editorChanged();
    });
    return axis;
}

/// Ask where to pass which planet, then aim the selected burn there.
void BurnPlanner::openAimDialog() {
    const int index = m_session->selected();
    if (index < 0 || m_session->isLocked(index)) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Aim at a planet"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* about = new QLabel(
        tr("Burn %1 will change so that your path passes the planet as you choose. Passing "
           "close bends your path and changes your speed around the Sun, with no fuel: a "
           "<b>gravity assist</b>. Closer passes and slower approaches bend it more.")
            .arg(index + 1),
        &dialog);
    about->setWordWrap(true);
    layout->addWidget(about);

    auto* form = new QFormLayout;
    auto* planet = new QComboBox(&dialog);
    planet->setObjectName(QStringLiteral("combo-aim-planet"));
    for (const orbit::Body p : orbit::kAllPlanets) {
        planet->addItem(QString::fromUtf8(orbit::bodyName(p).data()), static_cast<int>(p));
    }
    // The planet the path passes next after this burn, else Jupiter.
    int chosen = static_cast<int>(orbit::Body::Jupiter);
    if (const auto* p = m_session->prediction()) {
        const double after = m_session->burns()[static_cast<std::size_t>(index)].epoch;
        for (const orbit::PlanetPass& pass : p->flybys) {
            if (pass.epoch > after) {
                chosen = static_cast<int>(pass.planet);
                break;
            }
        }
    }
    planet->setCurrentIndex(planet->findData(chosen));
    auto* radii = new QDoubleSpinBox(&dialog);
    radii->setObjectName(QStringLiteral("spin-aim-radii"));
    radii->setRange(1.05, 2000.0);
    radii->setDecimals(2);
    radii->setValue(10.0);
    radii->setSuffix(tr(" × its radius"));
    radii->setToolTip(tr("How close to pass, from the planet's centre, in planet radii. 1 is "
                         "skimming the surface (or the cloud tops)."));
    auto* side = new QComboBox(&dialog);
    side->setObjectName(QStringLiteral("combo-aim-side"));
    side->addItem(tr("Keep the side you pass now"), static_cast<int>(orbit::FlybySide::Keep));
    side->addItem(tr("Speed up the most (usually behind it)"),
                  static_cast<int>(orbit::FlybySide::Faster));
    side->addItem(tr("Slow down the most (usually in front)"),
                  static_cast<int>(orbit::FlybySide::Slower));
    side->addItem(tr("Over its north side"), static_cast<int>(orbit::FlybySide::North));
    side->addItem(tr("Under its south side"), static_cast<int>(orbit::FlybySide::South));
    side->setCurrentIndex(1);
    form->addRow(tr("Body"), planet);
    form->addRow(tr("Closest pass"), radii);
    form->addRow(tr("Side"), side);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Aim"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    const auto p = static_cast<orbit::Body>(planet->currentData().toInt());
    m_session->aimBurn(index, {p, radii->value() * orbit::bodyRadius(p),
                               static_cast<orbit::FlybySide>(side->currentData().toInt())});
}

/// Send the editor's values to the session.
void BurnPlanner::editorChanged() {
    if (m_updating) return;
    const int index = m_session->selected();
    if (index < 0) return;
    const orbit::Burn burn{m_session->now() + m_whenSpin->value() * kDay, m_prograde.spin->value(),
                           m_radial.spin->value(), m_normal.spin->value()};
    m_session->setBurn(index, burn);
}

void BurnPlanner::refresh() {
    m_updating = true;
    const bool playing = m_session->hasPuzzle();
    m_add->setEnabled(playing);
    m_hint->setEnabled(playing && !m_session->isBusy());
    m_aim->setEnabled(!m_session->isBusy());

    // The list: one row per burn, the fired ones greyed.
    const auto& burns = m_session->burns();
    m_list->clear();
    for (int i = 0; i < m_session->burnCount(); ++i) {
        const orbit::Burn& b = burns[static_cast<std::size_t>(i)];
        auto* item = new QListWidgetItem(
            tr("%1.  %2   %3")
                .arg(i + 1)
                .arg(display::date(b.epoch), display::speed(orbit::burnSize(b))),
            m_list);
        if (m_session->isLocked(i)) {
            item->setText(item->text() + tr("   (fired)"));
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
    }
    const int selected = m_session->selected();
    m_list->setCurrentRow(selected);

    // The editor, for a burn still to come.
    const bool editable = selected >= 0 && !m_session->isLocked(selected);
    m_editor->setEnabled(editable);
    m_remove->setEnabled(editable);
    if (selected >= 0) {
        const orbit::Burn& b = burns[static_cast<std::size_t>(selected)];
        const double days = (b.epoch - m_session->now()) / kDay;
        m_whenSpin->setValue(days);
        m_whenSlider->setValue(static_cast<int>(std::lround(days * 10.0)));
        // Not before now (a burn can't be in the past), and as far ahead as the spin box goes.
        m_whenDate->setDateTimeRange(
            display::dateTime(m_session->now()),
            display::dateTime(m_session->now() + m_whenSpin->maximum() * kDay));
        m_whenDate->setDateTime(display::dateTime(b.epoch));
        for (auto [axis, value] :
             {std::pair{&m_prograde, b.prograde}, std::pair{&m_radial, b.radial},
              std::pair{&m_normal, b.normal}}) {
            axis->spin->setValue(value);
            axis->slider->setValue(static_cast<int>(std::lround(value)));
        }
        m_size->setText(tr("Size: %1").arg(display::speed(orbit::burnSize(b))));
    } else {
        m_size->setText(m_session->burnCount() == 0
                            ? tr("No burns yet. Add one, or ask for a hint.")
                            : tr("Pick a burn in the list to change it."));
    }
    m_updating = false;
    refreshFuel();
}

/// The fuel gauge: what is left after every planned burn, from the
/// prediction.
void BurnPlanner::refreshFuel() {
    if (!m_session->hasPuzzle()) {
        m_fuel->setValue(0);
        m_fuelText->clear();
        return;
    }
    const double budget = m_session->puzzle().budget;
    const auto* prediction = m_session->prediction();
    const double left = prediction ? prediction->deltaVLeft : budget;
    m_fuel->setValue(static_cast<int>(std::lround(1000.0 * std::clamp(left / budget, 0.0, 1.0))));
    QString text =
        tr("%1 of %2 left after these burns.").arg(display::speed(left), display::speed(budget));
    if (prediction) {
        const bool dry =
            std::ranges::any_of(prediction->burns, [](const auto& b) { return b.shortOfFuel; });
        if (dry) text += QLatin1Char(' ') + tr("<b>Not enough fuel for them all!</b>");
    }
    m_fuelText->setText(text);
}
