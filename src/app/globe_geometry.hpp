// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QQuick3DGeometry>
#include <QtQml/qqmlregistration.h>

/// A unit sphere whose texture coordinates follow latitude and longitude, so
/// an equirectangular map (like NASA's Blue Marble) lands in the right place:
/// longitude −180° at u = 0, 0° at u = 0.5; the north pole at the top of the
/// image. Vertices are in scene axes (see scene_coordinates.hpp) for the
/// Earth-fixed frame, so longitude 0 lies along +x and the north pole along +y.
class GlobeGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit GlobeGeometry(QQuick3DObject* parent = nullptr);

private:
    void build();
};
