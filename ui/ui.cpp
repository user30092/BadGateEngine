#include "ui.hpp"
#include "../render/gl_headers.hpp"     // GLES3/gl3.h или GL/glew.h
#include <cstddef>
#include <cstdio>

namespace mc {

// #version — теперь добавляет Shader::build() (платформенно: 300 es / 330 core).
static const char* UI_VS = R"(
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
uniform vec2 uScreen;
out vec4 vColor;
void main() {
    vec2 p = aPos / uScreen * 2.0 - 1.0;
    p.y = -p.y;
    gl_Position = vec4(p, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* UI_FS = R"(
precision mediump float;
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";

// ---- Пиксельный шрифт 5x7 ----
// (без изменений по сравнению с оригиналом)
struct Glyph { unsigned char rows[7]; };

static const Glyph GLYPHS[42] = {
    // --- цифры 0-9 ---
    {{0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}}, // 0
    {{0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}}, // 1
    {{0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}}, // 2
    {{0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110}}, // 3
    {{0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}}, // 4
    {{0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}}, // 5
    {{0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}}, // 6
    {{0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}}, // 7
    {{0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}}, // 8
    {{0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}}, // 9
    // --- буквы A-Z ---
    {{0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}}, // A
    {{0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110}}, // B
    {{0b01111,0b10000,0b10000,0b10000,0b10000,0b10000,0b01111}}, // C
    {{0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110}}, // D
    {{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111}}, // E
    {{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000}}, // F
    {{0b01111,0b10000,0b10000,0b10011,0b10001,0b10001,0b01111}}, // G
    {{0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001}}, // H
    {{0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110}}, // I
    {{0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100}}, // J
    {{0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001}}, // K
    {{0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111}}, // L
    {{0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001}}, // M
    {{0b10001,0b11001,0b10101,0b10101,0b10011,0b10001,0b10001}}, // N
    {{0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}}, // O
    {{0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000}}, // P
    {{0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101}}, // Q
    {{0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001}}, // R
    {{0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}}, // S
    {{0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100}}, // T
    {{0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110}}, // U
    {{0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100}}, // V
    {{0b10001,0b10001,0b10001,0b10101,0b10101,0b10101,0b01010}}, // W
    {{0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001}}, // X
    {{0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100}}, // Y
    {{0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111}}, // Z
    // --- символы ---
    {{0,0,0,0,0,0,0}},                                           // ' '
    {{0b00000,0b00100,0b00000,0b00000,0b00100,0b00000,0b00000}}, // ':'
    {{0b00000,0b00000,0b00000,0b00000,0b00000,0b01100,0b01100}}, // '.'
    {{0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000}}, // '-'
    {{0b00000,0b00100,0b00100,0b11111,0b00100,0b00100,0b00000}}, // '+'
    {{0b00001,0b00010,0b00010,0b00100,0b01000,0b01000,0b10000}}, // '/'
};

static int glyphIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    switch (c) {
        case ' ': return 36;
        case ':': return 37;
        case '.': return 38;
        case '-': return 39;
        case '+': return 40;
        case '/': return 41;
        default:  return -1;
    }
}

bool UIRenderer::init() {
    return shader.build(UI_VS, UI_FS);
}

void UIRenderer::begin(int w, int h) {
    scrW = w; scrH = h;
    verts.clear();
}

void UIRenderer::pushQuad(float x, float y, float w, float h, Vec4 c) {
    UIVertex tl{x,   y,   c.x, c.y, c.z, c.w};
    UIVertex tr{x+w, y,   c.x, c.y, c.z, c.w};
    UIVertex br{x+w, y+h, c.x, c.y, c.z, c.w};
    UIVertex bl{x,   y+h, c.x, c.y, c.z, c.w};
    verts.push_back(tl); verts.push_back(tr); verts.push_back(br);
    verts.push_back(tl); verts.push_back(br); verts.push_back(bl);
}

void UIRenderer::drawRect(float x, float y, float w, float h, Vec4 c) {
    pushQuad(x, y, w, h, c);
}

void UIRenderer::drawCrosshair(int w, int h) {
    float cx = w * 0.5f, cy = h * 0.5f;
    Vec4 c{1, 1, 1, 0.8f};
    drawRect(cx - 12, cy - 1, 24, 2, c);
    drawRect(cx - 1, cy - 12, 2, 24, c);
}

void UIRenderer::drawJoystick(Vec2 center, Vec2 knob, float radius, bool active) {
    if (!active) return;
    drawRect(center.x - radius, center.y - radius, radius*2, radius*2, Vec4{1,1,1,0.15f});
    float kr = radius * 0.35f;
    drawRect(knob.x - kr, knob.y - kr, kr*2, kr*2, Vec4{1,1,1,0.4f});
}

void UIRenderer::drawHotbar(int w, int h, int selected, int slots) {
    float slotSize = 64.0f;
    float pad = 4.0f;
    float totalW = slots * slotSize + (slots - 1) * pad;
    float sx = (w - totalW) * 0.5f;
    float sy = h - slotSize - 20.0f;

    for (int i = 0; i < slots; ++i) {
        float x = sx + i * (slotSize + pad);
        Vec4 bg = (i == selected) ? Vec4{1,1,1,0.5f} : Vec4{0,0,0,0.35f};
        drawRect(x, sy, slotSize, slotSize, bg);
        Vec4 border{1,1,1,0.6f};
        drawRect(x, sy, slotSize, 2, border);
        drawRect(x, sy + slotSize - 2, slotSize, 2, border);
        drawRect(x, sy, 2, slotSize, border);
        drawRect(x + slotSize - 2, sy, 2, slotSize, border);
    }
}

void UIRenderer::drawGlyph(char c, float x, float y, float scale, Vec4 color) {
    int idx = glyphIndex(c);
    if (idx < 0) return;
    const Glyph& g = GLYPHS[idx];

    for (int row = 0; row < 7; ++row) {
        unsigned char bits = g.rows[row];
        for (int col = 0; col < 5; ++col) {
            if (bits & (1 << (4 - col))) {
                drawRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void UIRenderer::drawNumber(int value, float x, float y, float scale, Vec4 color) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value);
    drawText(buf, x, y, scale, color);
}

void UIRenderer::drawText(const char* str, float x, float y, float scale, Vec4 color) {
    float cx = x;
    for (const char* p = str; *p; ++p) {
        drawGlyph(*p, cx, y, scale, color);
        cx += (5 + 1) * scale;
    }
}

float UIRenderer::textWidth(const char* str, float scale) {
    int len = 0;
    for (const char* p = str; *p; ++p) ++len;
    if (len == 0) return 0.0f;
    return len * 5.0f * scale + (len - 1) * scale;
}

void UIRenderer::drawButton(Rect r, const char* label, bool pressed) {
    Vec4 bg = pressed ? Vec4{1, 1, 1, 0.55f} : Vec4{0, 0, 0, 0.45f};
    drawRect(r.x, r.y, r.w, r.h, bg);

    Vec4 border{1, 1, 1, 0.7f};
    constexpr float t = 3.0f;
    drawRect(r.x,           r.y,           r.w, t, border);
    drawRect(r.x,           r.y + r.h - t, r.w, t, border);
    drawRect(r.x,           r.y,           t,   r.h, border);
    drawRect(r.x + r.w - t, r.y,           t,   r.h, border);

    if (label) {
        int len = 0;
        for (const char* p = label; *p; ++p) ++len;
        if (len > 0) {
            float maxTextW = r.w - 16.0f;
            float scale = (maxTextW > 0) ? maxTextW / (len * 6.0f - 1.0f) : 1.0f;
            scale = clampf(scale, 1.0f, 4.0f);
            float glyphH = 7.0f * scale;
            float totalW = textWidth(label, scale);

            float tx = r.x + (r.w - totalW) * 0.5f;
            float ty = r.y + (r.h - glyphH) * 0.5f;
            drawText(label, tx, ty, scale, Vec4{1, 1, 1, 1});
        }
    }
}

void UIRenderer::flush() {
    if (verts.empty()) return;

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader.use();
    glUniform2f(glGetUniformLocation(shader.id(), "uScreen"),
                static_cast<float>(scrW), static_cast<float>(scrH));

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(UIVertex)),
                 verts.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
                          reinterpret_cast<void*>(offsetof(UIVertex, r)));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

} // namespace mc
