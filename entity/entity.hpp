#pragma once
#include "../core/math.hpp"

namespace mc {

    class Entity {
    public:
        virtual ~Entity() = default;

        Vec3 position{0, 50, 0};
        Vec3 velocity{0, 0, 0};
        float yaw = 0.0f;
        float pitch = 0.0f;
        bool onGround = false;

        Vec3 forward() const {
            float cp = std::cos(pitch * DEG2RAD);
            return {
                    -std::sin(yaw * DEG2RAD) * cp,
                    std::sin(pitch * DEG2RAD),
                    -std::cos(yaw * DEG2RAD) * cp
            };
        }

        // FIX: right() должен быть ортогонален forward() в плоскости XZ.
        // Старый вариант {cos(yaw), 0, sin(yaw)} не был перпендикулярен
        // forward = {-sin(yaw), 0, -cos(yaw)} — их скалярное произведение
        // давало -sin(2*yaw)/2 ≠ 0, из-за чего moveForward/moveRight
        // «уводили» персонажа при любом повороте камеры.
        // Правильный right = forward × up = {cos(yaw), 0, -sin(yaw)}.
        Vec3 right() const {
            return { std::cos(yaw * DEG2RAD), 0, -std::sin(yaw * DEG2RAD) };
        }

        virtual void update(float dt) = 0;
    };

} // namespace mc