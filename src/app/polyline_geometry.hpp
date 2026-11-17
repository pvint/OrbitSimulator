// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QQuick3DGeometry>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

/// A connected line through a list of points, each with its own colour.
/// Used for the trajectory and the ground track. Use it with a material that
/// has vertexColorsEnabled: true.
class PolylineGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PolylineGeometry(QQuick3DObject* parent = nullptr);

    /// Replace the line. `colors` must have one entry per point.
    void setPoints(const QList<QVector3D>& points, const QList<QColor>& colors);
};
