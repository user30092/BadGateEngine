#pragma once
#include "world.hpp"
#include "block.hpp"
#include "../core/math.hpp"
#include <cmath>

namespace mc {

struct RayHit {
    bool hit = false;
    int bx = 0, by = 0, bz = 0;
    int px = 0, py = 0, pz = 0;   // соседний блок (для place)
};

// Amanatides & Woo — voxel ray traversal.
inline RayHit raycast(World& world, const Vec3& origin, const Vec3& dir, float maxDist) {
    RayHit result;

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    int stepX = (dir.x > 0) ? 1 : -1;
    int stepY = (dir.y > 0) ? 1 : -1;
    int stepZ = (dir.z > 0) ? 1 : -1;

    float tDeltaX = std::abs(1.0f / (dir.x == 0 ? 1e-8f : dir.x));
    float tDeltaY = std::abs(1.0f / (dir.y == 0 ? 1e-8f : dir.y));
    float tDeltaZ = std::abs(1.0f / (dir.z == 0 ? 1e-8f : dir.z));

    float tMaxX = ((dir.x > 0 ? (x + 1 - origin.x) : (origin.x - x))) * tDeltaX;
    float tMaxY = ((dir.y > 0 ? (y + 1 - origin.y) : (origin.y - y))) * tDeltaY;
    float tMaxZ = ((dir.z > 0 ? (z + 1 - origin.z) : (origin.z - z))) * tDeltaZ;

    int lastX = x, lastY = y, lastZ = z;

    while (true) {
        if (isSolid(world.getBlock(x, y, z))) {
            result.hit = true;
            result.bx = x; result.by = y; result.bz = z;
            result.px = lastX; result.py = lastY; result.pz = lastZ;
            return result;
        }
        lastX = x; lastY = y; lastZ = z;

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            if (tMaxX > maxDist) break;
            x += stepX; tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            if (tMaxY > maxDist) break;
            y += stepY; tMaxY += tDeltaY;
        } else {
            if (tMaxZ > maxDist) break;
            z += stepZ; tMaxZ += tDeltaZ;
        }
    }
    return result;
}

} // namespace mc
