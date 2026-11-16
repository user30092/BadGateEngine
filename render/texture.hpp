#pragma once
#include "gl_headers.hpp"
#include <cstdint>

namespace mc {

class Texture {
public:
    ~Texture();
    bool createFromPixels(const uint8_t* rgba, int w, int h);
    void bind(int slot = 0) const;
    GLuint id() const { return tex; }

private:
    GLuint tex = 0;
};

// Процедурный атлас 16x16 тайлов (256x256 пикселей, 16px на тайл).
// ВАЖНО: 16 тайлов в ряд жёстко прибито и здесь, и в render/mesh_builder.cpp
// (addFace(): TILES_PER_ROW = 16.0f) — если меняете одно, меняйте и другое.
// Индексы тайлов 0-9 соответствуют texTop/texSide/texBottom из
// world/block.hpp; 10-15 — "текстура не найдена" (шахматка маджента/чёрный).
void buildDefaultAtlas(uint8_t* outRGBA, int atlasSize);

} // namespace mc
