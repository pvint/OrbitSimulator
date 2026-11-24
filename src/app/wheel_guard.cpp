// SPDX-License-Identifier: GPL-3.0-or-later
#include "wheel_guard.hpp"

#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QScrollBar>
#include <QWidget>

namespace {

/// A control the wheel changes: a spin box (and date edit), a slider or a
/// drop-down list.
bool isValueControl(const QWidget* w) {
    return qobject_cast<const QAbstractSpinBox*>(w) != nullptr ||
           qobject_cast<const QComboBox*>(w) != nullptr ||
           (qobject_cast<const QAbstractSlider*>(w) != nullptr &&
            qobject_cast<const QScrollBar*>(w) == nullptr);
}

/// The control a wheel event is for: the widget itself, or the one it is
/// part of (a spin box's text field gets the event first).
QWidget* valueControlOf(QWidget* w) {
    for (; w != nullptr && !w->isWindow(); w = w->parentWidget()) {
        if (isValueControl(w)) return w;
    }
    return nullptr;
}

/// The nearest scrolling area around a widget (not counting the widget).
QAbstractScrollArea* scrollAreaAround(QWidget* w) {
    for (QWidget* p = w->parentWidget(); p != nullptr; p = p->parentWidget()) {
        if (auto* area = qobject_cast<QAbstractScrollArea*>(p)) return area;
        if (p->isWindow()) break; // (checked after: the area may be the window)
    }
    return nullptr;
}

} // namespace

bool WheelGuard::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr) return false;

    if (event->type() == QEvent::Polish && isValueControl(widget) &&
        widget->focusPolicy() == Qt::WheelFocus) {
        widget->setFocusPolicy(Qt::StrongFocus);
        return false;
    }
    if (event->type() != QEvent::Wheel) return false;

    QWidget* control = valueControlOf(widget);
    if (control == nullptr || control->hasFocus()) return false;
    QAbstractScrollArea* area = scrollAreaAround(control);
    if (area == nullptr) return false;
    // Scroll the area instead. Its viewport turns the wheel into scrolling.
    QCoreApplication::sendEvent(area->viewport(), event);
    return true;
}
