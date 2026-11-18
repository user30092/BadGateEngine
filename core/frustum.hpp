#pragma once
#include <cmath>
#include "math.hpp"
#include "types.hpp"

namespace mc {

struct Plane {
    float a = 0, b = 0, c = 0, d = 0;

    void normalize() {
        float len = std::sqrt(a*a + b*b + c*c);
        if (len > 1e-8f) {
            float inv = 1.0f / len;
            a *= inv; b *= inv; c *= inv; d *= inv;
        }
    }

    float distance(const Vec3& p) const {
        return a * p.x + b * p.y + c * p.z + d;
    }
};

struct AABB {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};

    Vec3 center() const {
        return {(min.x + max.x) * 0.5f,
                (min.y + max.y) * 0.5f,
                (min.z + max.z) * 0.5f};
    }
};

class Frustum {
public:
    void extractFrom(const Mat4& vp) {
        auto row = [&](int r) -> Vec4 {
            return { vp.m[r], vp.m[4 + r], vp.m[8 + r], vp.m[12 + r] };
        };
        const Vec4 r1 = row(0);
        const Vec4 r2 = row(1);
        const Vec4 r3 = row(2);
        const Vec4 r4 = row(3);

        planes[0] = { r4.x + r1.x, r4.y + r1.y, r4.z + r1.z, r4.w + r1.w }; // left
        planes[1] = { r4.x - r1.x, r4.y - r1.y, r4.z - r1.z, r4.w - r1.w }; // right
        planes[2] = { r4.x + r2.x, r4.y + r2.y, r4.z + r2.z, r4.w + r2.w }; // bottom
        planes[3] = { r4.x - r2.x, r4.y - r2.y, r4.z - r2.z, r4.w - r2.w }; // top
        planes[4] = { r4.x + r3.x, r4.y + r3.y, r4.z + r3.z, r4.w + r3.w }; // near
        planes[5] = { r4.x - r3.x, r4.y - r3.y, r4.z - r3.z, r4.w - r3.w }; // far

        for (auto& p : planes) p.normalize();
    }


    bool intersects(const AABB& box) const {
        for (const auto& p : planes) {
            float px = (p.a >= 0) ? box.max.x : box.min.x;
            float py = (p.b >= 0) ? box.max.y : box.min.y;
            float pz = (p.c >= 0) ? box.max.z : box.min.z;
            if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) return false;
        }
        return true;
    }

    const Plane& plane(int i) const { return planes[i]; }

private:
    Plane planes[6];
};

inline AABB chunkAABB(ChunkPos cx, ChunkPos cz) {
    AABB b;
    b.min = { static_cast<float>(cx)     * CHUNK_SIZE, 0.0f,
              static_cast<float>(cz)     * CHUNK_SIZE };
    b.max = { static_cast<float>(cx + 1) * CHUNK_SIZE,
              static_cast<float>(CHUNK_HEIGHT),
              static_cast<float>(cz + 1) * CHUNK_SIZE };
    return b;
}

} // namespace mc
