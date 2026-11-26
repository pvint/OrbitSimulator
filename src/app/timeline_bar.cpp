// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_bar.hpp"

#include "display_format.hpp"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
constexpr double kDay = 86400.0;
constexpr int kMargin = 14;
constexpr int kLabelSpace = 90; ///< least pixels between two date labels
} // namespace

TimelineBar::TimelineBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("timeline-bar"));
    setMinimumHeight(40);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click or drag to go to a moment in the flight. Double-click to type a date."));
}

QSize TimelineBar::sizeHint() const { return {400, 44}; }

void TimelineBar::setRange(double start, double end) {
    m_start = start;
    m_end = std::max(end, start + kDay);
    update();
}

void TimelineBar::setNow(double epoch) {
    m_now = epoch;
    update();
}

void TimelineBar::setBurns(std::vector<double> epochs) {
    m_burns = std::move(epochs);
    update();
}

QRectF TimelineBar::track() const {
    return {static_cast<double>(kMargin), 8.0, static_cast<double>(width() - 2 * kMargin), 10.0};
}

double TimelineBar::xAt(double epoch) const {
    const QRectF t = track();
    return t.left() + t.width() * (epoch - m_start) / (m_end - m_start);
}

double TimelineBar::epochAt(double x) const {
    const QRectF t = track();
    const double f = std::clamp((x - t.left()) / t.width(), 0.0, 1.0);
    return m_start + f * (m_end - m_start);
}

void TimelineBar::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF t = track();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x47, 0x55, 0x69));
    p.drawRoundedRect(t, 3, 3);
    // The part already flown.
    const QRectF flown(t.left(), t.top(), std::max(0.0, xAt(m_now) - t.left()), t.height());
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawRoundedRect(flown, 3, 3);

    // Date ticks: the first step from this list that leaves room for labels.
    // (Days; whole years for trips of decades.)
    constexpr std::array<double, 10> steps{5,       10,     30,    60,      91.3125,
                                           182.625, 365.25, 730.5, 1826.25, 3652.5};
    double step = steps.back();
    for (const double s : steps) {
        if (t.width() * s * kDay / (m_end - m_start) >= kLabelSpace) {
            step = s;
            break;
        }
    }
    p.setFont(font());
    p.setPen(palette().color(QPalette::Text));
    const QFontMetrics fm(font());
    const auto ticks = static_cast<int>(std::floor((m_end + 1.0 - m_start) / (step * kDay)));
    for (int i = 0; i <= ticks; ++i) {
        const double e = m_start + i * step * kDay;
        const double x = xAt(e);
        p.drawLine(QPointF(x, t.bottom() + 1), QPointF(x, t.bottom() + 5));
        const QString label = display::date(e);
        const double w = fm.horizontalAdvance(label);
        const double left = std::clamp(x - w / 2.0, 0.0, width() - w);
        p.drawText(QPointF(left, t.bottom() + 6 + fm.ascent()), label);
    }

    // Burns: orange diamonds.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xf5, 0x9e, 0x0b));
    for (const double e : m_burns) {
        const QPointF c(xAt(e), t.center().y());
        p.drawPolygon(QPolygonF(
            {c + QPointF(0, -7), c + QPointF(5, 0), c + QPointF(0, 7), c + QPointF(-5, 0)}));
    }

    // Now: a white handle.
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(0x0b, 0x12, 0x20), 1.5));
    p.drawRoundedRect(QRectF(xAt(m_now) - 3, t.top() - 4, 6, t.height() + 8), 2, 2);
}

void TimelineBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) emit timeSelected(epochAt(event->position().x()));
}

void TimelineBar::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) emit timeSelected(epochAt(event->position().x()));
}

void TimelineBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) emit dateRequested();
}
