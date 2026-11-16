#pragma once
#include "entity.hpp"
#include "../world/block.hpp"
#include "../world/world.hpp"
#include <cmath>
#include <algorithm>

namespace mc {

    class Player : public Entity {
    public:
        explicit Player(World* w) : world(w) {}

        bool flying = false;

        void toggleFlying() {
            flying     = !flying;
            velocity.y = 0.0f;
        }

        void update(float dt) override {
            if (!flying) {
                velocity.y -= 28.0f * dt;   // гравитация только в обычном режиме
            }
            moveAxis(0, velocity.x * dt);
            moveAxis(1, velocity.y * dt);
            moveAxis(2, velocity.z * dt);
            // Горизонтальное затухание есть всегда, вертикаль управляется снаружи
            velocity.x *= 0.85f;
            velocity.z *= 0.85f;
        }

        void jump() {
            if (flying) { velocity.y = 8.0f; return; }
            if (onGround) {
                velocity.y = 9.0f;
                onGround   = false;
            }
        }

        // FIX: движение вперёд должно игнорировать pitch. Раньше здесь
        // использовался forward() (с cos(pitch)), и при взгляде почти
        // вертикально вниз/вверх f.x и f.z становились ~0 — после
        // normalized() получалось мусорное направление, и персонаж
        // дёргался в случайную сторону. Теперь берём чистый yaw.
        void moveForward(float amount) {
            float sy = std::sin(yaw * DEG2RAD);
            float cy = std::cos(yaw * DEG2RAD);
            velocity.x += -sy * amount;
            velocity.z += -cy * amount;
        }

        void moveRight(float amount) {
            float sy = std::sin(yaw * DEG2RAD);
            float cy = std::cos(yaw * DEG2RAD);
            velocity.x +=  cy * amount;
            velocity.z +=  -sy * amount;
        }


        Vec3 eyePos() const { return {position.x, position.y + 1.62f, position.z}; }

    private:
        // FIX: (1) не делаем return после первой коллизии по X/Z — за один
        // кадр можно пересечь несколько блоков, и нужно учитывать ВСЕ,
        // выбирая крайнюю позицию (min/max). Иначе после первого return
        // игрок оказывался внутри второго блока (протискивание в углы).
        // (2) большие delta разбиваем на подшаги ≤ 0.1 блока — иначе при
        // большом dt (первый кадр, лаг) игрок проскакивает стену между
        // кадрами (tunneling).
        void moveAxis(int axis, float delta) {
            if (delta == 0.0f) return;

            constexpr float MAX_STEP = 0.1f;
            int steps = static_cast<int>(std::ceil(std::abs(delta) / MAX_STEP));
            if (steps < 1) steps = 1;
            float step = delta / static_cast<float>(steps);

            for (int s = 0; s < steps; ++s) {
                if (!stepAxis(axis, step)) break;
            }
        }

        // Возвращает false, если произошла коллизия (дальше двигать смысла нет).
        bool stepAxis(int axis, float delta) {
            if (delta == 0.0f) return true;

            float& pos = (axis == 0) ? position.x : (axis == 1 ? position.y : position.z);
            pos += delta;

            const float halfW  = 0.3f;
            const float height = 1.8f;
            constexpr float EPS = 1e-3f;

            // Границы AABB после сдвига (один раз, до цикла).
            const float aMinX = position.x - halfW;
            const float aMaxX = position.x + halfW;
            const float aMinY = position.y;
            const float aMaxY = position.y + height;
            const float aMinZ = position.z - halfW;
            const float aMaxZ = position.z + halfW;

            // Полуоткрытый интервал: floor(max - EPS) НЕ включает блок,
            // к которому мы только касаемся правой/верхней/передней гранью.
            int x0 = static_cast<int>(std::floor(aMinX));
            int x1 = static_cast<int>(std::floor(aMaxX - EPS));
            int y0 = static_cast<int>(std::floor(aMinY));
            int y1 = static_cast<int>(std::floor(aMaxY - EPS));
            int z0 = static_cast<int>(std::floor(aMinZ));
            int z1 = static_cast<int>(std::floor(aMaxZ - EPS));

            // Считаем самую консервативную границу отката.
            float target = pos;
            bool collided = false;

            for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                if (!isSolid(world->getBlock(x, y, z))) continue;

                // Строгое пересечение AABB с блоком (касание исключено эпсилоном).
                if (!(aMaxX > x + EPS && aMinX < x + 1 - EPS &&
                      aMaxY > y + EPS && aMinY < y + 1 - EPS &&
                      aMaxZ > z + EPS && aMinZ < z + 1 - EPS)) continue;

                collided = true;

                if (axis == 1) {
                    if (delta < 0) {
                        // Падение — встаём на верхнюю грань блока.
                        float candidate = static_cast<float>(y + 1);
                        if (candidate > target) target = candidate;
                        onGround = true;
                    } else {
                        // Прыжок — упираемся головой в нижнюю грань.
                        float candidate = static_cast<float>(y) - height - EPS;
                        if (candidate < target) target = candidate;
                    }
                } else if (axis == 0) {
                    if (delta < 0) {
                        // Движение в −X — прижимаемся к правой грани.
                        float candidate = static_cast<float>(x + 1) + halfW + EPS;
                        if (candidate > target) target = candidate;
                    } else {
                        // Движение в +X — прижимаемся к левой грани.
                        float candidate = static_cast<float>(x) - halfW - EPS;
                        if (candidate < target) target = candidate;
                    }
                } else {
                    if (delta < 0) {
                        float candidate = static_cast<float>(z + 1) + halfW + EPS;
                        if (candidate > target) target = candidate;
                    } else {
                        float candidate = static_cast<float>(z) - halfW - EPS;
                        if (candidate < target) target = candidate;
                    }
                }
            }

            if (collided) {
                pos = target;
                if      (axis == 0) velocity.x = 0.0f;
                else if (axis == 1) velocity.y = 0.0f;
                else                velocity.z = 0.0f;
                return false;
            }
            return true;
        }

        World* world;
    };

} // namespace mc
