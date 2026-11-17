// SPDX-License-Identifier: GPL-3.0-or-later
#include "globe_geometry.hpp"

#include <QByteArray>
#include <QVector3D>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

namespace {

// 1° per segment: smooth even when zoomed in close.
constexpr int kLatitudeSegments = 180;
constexpr int kLongitudeSegments = 360;

struct Vertex {
    float x, y, z;    // position (= normal, on a unit sphere)
    float nx, ny, nz; // normal
    float u, v;       // texture coordinates
};

/// Copy a vector's contents into the byte array Qt Quick 3D wants.
template <typename T>
QByteArray toBytes(const std::vector<T>& items) {
    QByteArray bytes(static_cast<qsizetype>(items.size() * sizeof(T)), Qt::Uninitialized);
    std::memcpy(bytes.data(), items.data(), items.size() * sizeof(T));
    return bytes;
}

} // namespace

GlobeGeometry::GlobeGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) { build(); }

void GlobeGeometry::build() {
    constexpr double pi = std::numbers::pi;
    constexpr int columns = kLongitudeSegments + 1; // the seam is duplicated so u runs 0 → 1
    constexpr int rows = kLatitudeSegments + 1;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(rows) * columns);

    for (int row = 0; row < rows; ++row) {
        // Row 0 is the north pole, the last row the south pole.
        const double lat = pi / 2.0 - pi * row / kLatitudeSegments;
        for (int col = 0; col < columns; ++col) {
            const double lon = -pi + 2.0 * pi * col / kLongitudeSegments;
            // Earth-fixed (x, y, z) = (cos lat cos lon, cos lat sin lon, sin lat),
            // in scene axes (x, z, −y).
            const auto x = static_cast<float>(std::cos(lat) * std::cos(lon));
            const auto y = static_cast<float>(std::sin(lat));
            const auto z = static_cast<float>(-std::cos(lat) * std::sin(lon));
            const auto u = static_cast<float>(col) / kLongitudeSegments;
            // Qt Quick 3D's v runs bottom → top, so the north pole is v = 1.
            const auto tv = 1.0F - static_cast<float>(row) / kLatitudeSegments;
            vertices.push_back({x, y, z, x, y, z, u, tv});
        }
    }

    // Two triangles per quad, wound counter-clockwise seen from outside.
    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(kLatitudeSegments) * kLongitudeSegments * 6);
    for (int row = 0; row < kLatitudeSegments; ++row) {
        for (int col = 0; col < kLongitudeSegments; ++col) {
            const auto a = static_cast<std::uint32_t>(row * columns + col); // top left
            const auto b = a + static_cast<std::uint32_t>(columns);         // bottom left
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }

    clear();
    setStride(sizeof(Vertex));
    setVertexData(toBytes(vertices));
    setIndexData(toBytes(indices));
    setPrimitiveType(PrimitiveType::Triangles);
    setBounds({-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F});
    addAttribute(Attribute::PositionSemantic, offsetof(Vertex, x), Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, offsetof(Vertex, nx), Attribute::F32Type);
    addAttribute(Attribute::TexCoord0Semantic, offsetof(Vertex, u), Attribute::F32Type);
    addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);
    update();
}
