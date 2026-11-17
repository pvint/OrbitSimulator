#include "orbit/core/vec3.hpp"

#include <ostream>

namespace orbit {

std::ostream& operator<<(std::ostream& os, const Vec3& v) {
    return os << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}

} // namespace orbit
