// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QQuick3DGeometry>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

/// A connected line through a list of points, each with its own colour
/// (or, with setPointCloud, the points alone). Used for paths, orbits and
/// the asteroid belt. Use it with a material that has vertexColorsEnabled.
class PolylineGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PolylineGeometry(QQuick3DObject* parent = nullptr);

    /// Replace the line. `colors` must have one entry per point.
    void setPoints(const QList<QVector3D>& points, const QList<QColor>& colors);
    /// The same points, unconnected (drawn one pixel each).
    void setPointCloud(const QList<QVector3D>& points, const QList<QColor>& colors);

private:
    void upload(const QList<QVector3D>& points, const QList<QColor>& colors,
                PrimitiveType primitive);
};
