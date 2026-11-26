// SPDX-License-Identifier: GPL-3.0-or-later
#include "polyline_geometry.hpp"

#include <QByteArray>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {
struct Vertex {
    float x, y, z;
    float r, g, b, a;
};
} // namespace

PolylineGeometry::PolylineGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {}

void PolylineGeometry::setPoints(const QList<QVector3D>& points, const QList<QColor>& colors) {
    upload(points, colors, PrimitiveType::LineStrip);
}

void PolylineGeometry::setPointCloud(const QList<QVector3D>& points, const QList<QColor>& colors) {
    upload(points, colors, PrimitiveType::Points);
}

void PolylineGeometry::upload(const QList<QVector3D>& points, const QList<QColor>& colors,
                              PrimitiveType primitive) {
    Q_ASSERT(points.size() == colors.size());

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(points.size()));
    QVector3D lo(1e30F, 1e30F, 1e30F);
    QVector3D hi(-1e30F, -1e30F, -1e30F);
    for (qsizetype i = 0; i < points.size(); ++i) {
        const QVector3D& p = points[i];
        const QColor& c = colors[i];
        vertices.push_back({p.x(), p.y(), p.z(), c.redF(), c.greenF(), c.blueF(), c.alphaF()});
        lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
        hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
    }

    clear();
    setStride(sizeof(Vertex));
    QByteArray data(static_cast<qsizetype>(vertices.size() * sizeof(Vertex)), Qt::Uninitialized);
    std::memcpy(data.data(), vertices.data(), vertices.size() * sizeof(Vertex));
    setVertexData(data);
    setPrimitiveType(primitive);
    if (!points.isEmpty()) setBounds(lo, hi);
    addAttribute(Attribute::PositionSemantic, offsetof(Vertex, x), Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, offsetof(Vertex, r), Attribute::F32Type);
    update();
}
