#pragma once
#include "../render/shader.hpp"
#include "../core/math.hpp"
#include "../input/input.hpp" // Rect
#include <vector>

namespace mc {

    class UIRenderer {
    public:
        bool init();
        void begin(int screenW, int screenH);
        void drawRect(float x, float y, float w, float h, Vec4 color);
        void drawCrosshair(int screenW, int screenH);
        void drawJoystick(Vec2 center, Vec2 knob, float radius, bool active);
        void drawHotbar(int screenW, int screenH, int selected, int slots);
        void drawButton(Rect r, const char* label, bool pressed);

        // Цифровой счётчик. Рисует число пиксельными глифами 5x7.
        // x, y — левый верхний угол. scale — размер пикселя глифа.
        [[maybe_unused]] void drawNumber(int value, float x, float y, float scale, Vec4 color);
        void drawText(const char* str, float x, float y, float scale, Vec4 color);

        // Ширина строки в пикселях при данном scale — для центрирования текста.
        static float textWidth(const char* str, float scale);

        void flush();

    private:
        struct UIVertex { float x, y, r, g, b, a; };

        Shader shader;
        std::vector<UIVertex> verts;
        int scrW = 0, scrH = 0;

        void pushQuad(float x, float y, float w, float h, Vec4 c);
        void drawGlyph(char c, float x, float y, float scale, Vec4 color);
    };

} // namespace mc