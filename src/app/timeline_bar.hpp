// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

#include <vector>

/// A time scale: the whole flight from start to end, a marker for each burn
/// and one for "now". Click anywhere, or drag, to jump to that moment.
class TimelineBar : public QWidget {
    Q_OBJECT
public:
    explicit TimelineBar(QWidget* parent = nullptr);

    /// The span shown, as epochs (s since J2000).
    void setRange(double start, double end);
    void setNow(double epoch);
    void setBurns(std::vector<double> epochs);

    QSize sizeHint() const override;

signals:
    /// The user picked this moment.
    void timeSelected(double epoch);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    double epochAt(double x) const;
    double xAt(double epoch) const;
    QRectF track() const;

    double m_start = 0.0;
    double m_end = 1.0;
    double m_now = 0.0;
    std::vector<double> m_burns;
};
