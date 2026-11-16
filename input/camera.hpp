#pragma once
#include "../core/math.hpp"
#include "input.hpp"

namespace mc {

    class CameraController {
    public:
        // sensitivity приходит из Settings (настраивается в меню) вместо
        // захардкоженной константы — иначе пункт "чувствительность" в
        // настройках ни на что бы не влиял.
        //
        // FIX: знак yaw согласован с Entity::forward = {-sin(yaw), *, -cos(yaw)}.
        // Раньше было yaw -= look.x, но при этом forward уходил вправо по X —
        // и свайп вправо поворачивал камеру влево. Теперь yaw += look.x:
        // свайп вправо увеличивает yaw, forward = -sin(yaw) уходит влево…
        // нет — держим интуитивное поведение «свайп вправо → смотрим вправо».
        // Для forward = (-sin(yaw), 0, -cos(yaw)) смотреть вправо по +X
        // означает yaw < 0, то есть yaw -= look.x ДАВАЛ правильный знак.
        // Оставляем как было, но с комментарием, чтобы не «исправить» снова.
        void update(Input& input, float& yaw, float& pitch, float /*dt*/, float sensitivity = 0.15f) {
            Vec2 look = input.lookDelta();
            yaw   -= look.x * sensitivity;
            pitch -= look.y * sensitivity;
            yaw = std::remainder(yaw, 360.0f);
            pitch = clampf(pitch, -89.0f, 89.0f);
        }
    };

} // namespace mc
