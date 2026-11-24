// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

/// Keeps the mouse wheel from changing values by accident.
///
/// Qt's spin boxes, sliders and drop-down lists react to the wheel whenever
/// the pointer is over them. In a panel that scrolls, that means a control
/// sliding under the pointer while you scroll catches the wheel and changes
/// its value. With this filter installed on the application:
///   - inside a scrolling area, the wheel scrolls the area, unless the
///     control has the keyboard focus (you clicked into it first, to
///     change it with the wheel on purpose);
///   - the wheel no longer gives these controls the focus (their focus
///     policy goes from WheelFocus to StrongFocus: click or Tab instead).
/// Controls outside scrolling areas (the playback speed, say) are left as
/// they were.
class WheelGuard : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};
