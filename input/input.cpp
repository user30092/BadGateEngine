#include "input.hpp"
#include <cmath>

namespace mc {

static constexpr float DEADZONE    = 0.15f;
static constexpr float MOVE_RADIUS = 150.0f;

void Input::onPointerDown(int32_t pointerId, float x, float y,
                          int screenW, int /*screenH*/) {
    bool isLeft = x < screenW * 0.5f;

    // Сначала кнопки — они поглощают нажатие.
    if (jumpButton.contains(x, y))  { jumpFlag  = true; return; }
    if (flyButton.contains(x, y))   { flyFlag   = true; return; }
    if (breakButton.contains(x, y)) { breakFlag = true; return; }
    if (placeButton.contains(x, y)) { placeFlag = true; return; }

    if (isLeft) {
        movePtr.active = true;
        movePtr.id = pointerId;
        movePtr.x = x; movePtr.y = y;
        movePtr.startX = x; movePtr.startY = y;
    } else {
        lookPtr.active = true;
        lookPtr.id = pointerId;
        lookPtr.x = x; lookPtr.y = y;
        lookPtr.startX = x; lookPtr.startY = y;
        lookPtr.curX = x;   lookPtr.curY = y;
    }
}

void Input::onPointerMove(int32_t pointerId, float x, float y) {
    if (movePtr.active && pointerId == movePtr.id) {
        float dx = x - movePtr.startX;
        float dy = y - movePtr.startY;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len > MOVE_RADIUS) {
            dx *= MOVE_RADIUS / len;
            dy *= MOVE_RADIUS / len;
        }
        moveVector.x = dx / MOVE_RADIUS;
        moveVector.y = dy / MOVE_RADIUS;

        // Dead-zone по длине вектора (не по осям!) — иначе на диагонали
        // при малых отклонениях одна ось обнулялась и направление рвалось.
        float mlen = std::sqrt(moveVector.x*moveVector.x + moveVector.y*moveVector.y);
        if (mlen < DEADZONE) {
            moveVector = {0, 0};
        } else if (mlen > 1.0f) {
            // Нормализация: по диагонали скорость не должна быть в √2 раз выше.
            moveVector.x /= mlen;
            moveVector.y /= mlen;
        }
    } else if (lookPtr.active && pointerId == lookPtr.id) {
        lookAccum.x += x - lookPtr.x;
        lookAccum.y += y - lookPtr.y;
        lookPtr.x = x;
        lookPtr.y = y;
        lookPtr.curX = x;
        lookPtr.curY = y;
    }
}

void Input::onPointerUp(int32_t pointerId, float /*x*/, float /*y*/) {
    if (movePtr.active && pointerId == movePtr.id) {
        movePtr.active = false;
        movePtr.id = -1;
        moveVector = {0, 0};
    } else if (lookPtr.active && pointerId == lookPtr.id) {
        lookPtr.active = false;
        lookPtr.id = -1;
        lookAccum = {0, 0}; // не тащим «хвост» look-а на следующее касание
    }
}

void Input::onPointerCancel() {
    movePtr.active = false;
    lookPtr.active = false;
    movePtr.id = -1;
    lookPtr.id = -1;
    moveVector = {0, 0};
    lookAccum = {0, 0};
}

} // namespace mc
