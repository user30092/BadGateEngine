#pragma once
#include "../core/math.hpp"
#include <cstdint>

namespace mc {

// Идентификатор указателя: на Android — реальный pointer id из
// AMotionEvent_getPointerId, на десктопе — 0 для мыши.
struct TouchPointer {
    bool    active   = false;
    int32_t id       = -1;
    float   x = 0, y = 0;
    float   startX = 0, startY = 0;
    float   curX   = 0, curY   = 0;
};

// Прямоугольник на экране (в пикселях).
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

class Input {
public:
    // ===== Тач (Android) =====
    void onPointerDown  (int32_t pointerId, float x, float y, int screenW, int screenH);
    void onPointerMove  (int32_t pointerId, float x, float y);
    void onPointerUp    (int32_t pointerId, float x, float y);
    void onPointerCancel();

    // ===== Клавиатура/мышь (Desktop) =====
    void setKeyboardMove(float x, float y) { keyboardMove = {x, y}; }
    void setBreakHold(bool v)              { breakHold  = v; }
    void setPlaceHold(bool v)              { placeHold  = v; }
    void addLookDelta(float dx, float dy)  { lookAccum.x += dx; lookAccum.y += dy; }
    void triggerJump()                     { jumpFlag = true; }
    bool breakHeld() const                 { return breakHold; }
    bool placeHeld() const                 { return placeHold; }

    // ===== Регистрация виртуальных кнопок (Android) =====
    void setJumpButton (Rect r) { jumpButton  = r; }
    void setBreakButton(Rect r) { breakButton = r; }
    void setPlaceButton(Rect r) { placeButton = r; }

    // ===== Запросы (общие) =====
    Vec2 moveAxis() const {
        if (moveVector.x != 0.0f || moveVector.y != 0.0f) return moveVector;
        return keyboardMove;
    }
    Vec2 lookDelta()       { Vec2 d = lookAccum; lookAccum = {}; return d; }
    bool jumpPressed()  { bool v = jumpFlag;  jumpFlag  = false; return v; }
    bool breakPressed() { bool v = breakFlag; breakFlag = false; return v; }
    bool placePressed() { bool v = placeFlag; placeFlag = false; return v; }

    void setInventoryHotbar(int slot) { hotbar = slot; }
    int  hotbarSlot() const           { return hotbar; }
    void setJumpHold(bool v)    { jumpHeld_   = v; }
    void setCrouchHold(bool v)  { crouchHeld_ = v; }
    bool jumpHeld() const       { return jumpHeld_; }
    bool crouchHeld() const     { return crouchHeld_; }

private:
    TouchPointer movePtr, lookPtr;
    Vec2 moveVector{0, 0};      // тач-джойстик
    Vec2 keyboardMove{0, 0};    // WASD
    Vec2 lookAccum{0, 0};
    bool jumpFlag = false, breakFlag = false, placeFlag = false;
    bool breakHold = false, placeHold = false;
    bool jumpHeld_   = false;
    bool crouchHeld_ = false;
    int  hotbar = 1;

    Rect jumpButton, breakButton, placeButton;
};

} // namespace mc
