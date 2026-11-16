#include "texture.hpp"
#include "../core/random.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace mc {

Texture::~Texture() {
    if (tex) glDeleteTextures(1, &tex);
}

bool Texture::createFromPixels(const uint8_t* rgba, int w, int h) {
    if (tex) glDeleteTextures(1, &tex);   // повторный вызов не плодит текстуры
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    return true;
}

void Texture::bind(int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, tex);
}

namespace {

inline void setPixel(uint8_t* out, int atlasSize, int x, int y,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= atlasSize || y >= atlasSize) return;
    size_t o = (static_cast<size_t>(y) * atlasSize + x) * 4;
    out[o + 0] = r; out[o + 1] = g; out[o + 2] = b; out[o + 3] = a;
}

// ---------------------------------------------------------------------------
// tile 0–9: базовые блоки
// ---------------------------------------------------------------------------

void fillNoisy(uint8_t* out, int atlasSize, int tileSize, int bx, int by,
               uint8_t r, uint8_t g, uint8_t b, int jitter, uint32_t seed) {
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            int n = static_cast<int>(hash3(x, y, 0, seed) % static_cast<uint32_t>(jitter * 2 + 1)) - jitter;
            uint8_t rr = static_cast<uint8_t>(std::clamp(r + n, 0, 255));
            uint8_t gg = static_cast<uint8_t>(std::clamp(g + n, 0, 255));
            uint8_t bb = static_cast<uint8_t>(std::clamp(b + n, 0, 255));
            setPixel(out, atlasSize, bx + x, by + y, rr, gg, bb, 255);
        }
    }
}

void fillGrassSide(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    int band = std::max(1, tileSize / 5);
    for (int y = 0; y < tileSize; ++y) {
        int sy = tileSize - 1 - y;   // перевёрнутая координата
        for (int x = 0; x < tileSize; ++x) {
            int n = static_cast<int>(hash3(x, sy, 4, seed) % 21) - 10;
            if (sy < band) {
                uint8_t g = static_cast<uint8_t>(std::clamp(120 + n, 0, 255));
                setPixel(out, atlasSize, bx + x, by + y,
                         static_cast<uint8_t>(g * 0.4f), g,
                         static_cast<uint8_t>(g * 0.25f), 255);
            } else {
                uint8_t v = static_cast<uint8_t>(std::clamp(110 + n, 0, 255));
                setPixel(out, atlasSize, bx + x, by + y,
                         v, static_cast<uint8_t>(v * 0.65f),
                         static_cast<uint8_t>(v * 0.45f), 255);
            }
        }
    }
}

void fillWoodSide(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    for (int x = 0; x < tileSize; ++x) {
        int band = static_cast<int>(hash3(x, 0, 0, seed) % 3);
        uint8_t base = static_cast<uint8_t>(90 + band * 12);
        for (int y = 0; y < tileSize; ++y) {
            int n = static_cast<int>(hash3(x, y, 1, seed) % 11) - 5;
            uint8_t v = static_cast<uint8_t>(std::clamp(static_cast<int>(base) + n, 0, 255));
            setPixel(out, atlasSize, bx + x, by + y,
                     v, static_cast<uint8_t>(v * 0.75f),
                     static_cast<uint8_t>(v * 0.5f), 255);
        }
    }
}

void fillWoodTop(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    float cx = tileSize * 0.5f, cy = tileSize * 0.5f;
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            int ring = static_cast<int>(d / 2.5f) % 2;
            int n = static_cast<int>(hash3(x, y, 2, seed) % 7) - 3;
            uint8_t base = ring ? 150 : 175;
            uint8_t v = static_cast<uint8_t>(std::clamp(static_cast<int>(base) + n, 0, 255));
            setPixel(out, atlasSize, bx + x, by + y,
                     v, static_cast<uint8_t>(v * 0.78f),
                     static_cast<uint8_t>(v * 0.52f), 255);
        }
    }
}

void fillLeaves(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            uint32_t h = hash3(x, y, 3, seed) % 100;
            if (h < 15) {
                setPixel(out, atlasSize, bx + x, by + y, 40, 70, 30, 255);
            } else {
                int n = static_cast<int>(h % 30) - 15;
                uint8_t g = static_cast<uint8_t>(std::clamp(95 + n, 0, 255));
                setPixel(out, atlasSize, bx + x, by + y,
                         static_cast<uint8_t>(g * 0.35f), g,
                         static_cast<uint8_t>(g * 0.3f), 255);
            }
        }
    }
}

void fillWater(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            float wave = std::sin((x + y) * 0.6f) * 10.0f;
            int n = static_cast<int>(hash3(x, y, 5, seed) % 11) - 5;
            uint8_t b = static_cast<uint8_t>(std::clamp(160 + static_cast<int>(wave) + n, 0, 255));
            setPixel(out, atlasSize, bx + x, by + y, 40, 90, b, 180);
        }
    }
}

// ---------------------------------------------------------------------------
// tile 10–12: snow / sandstone / gravel
// ---------------------------------------------------------------------------

void fillSnow(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            int n = static_cast<int>(hash3(x, y, 0, seed) % 13) - 6;
            uint8_t v = static_cast<uint8_t>(std::clamp(240 + n, 0, 255));
            // Холодный оттенок: чуть больше синего.
            setPixel(out, atlasSize, bx + x, by + y,
                     v, v, static_cast<uint8_t>(std::min(255, v + 8)), 255);
        }
    }
}

void fillSandstone(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    // Горизонтальные полосы: 3 «слоя» с немного разной яркостью.
    for (int y = 0; y < tileSize; ++y) {
        int band = (y / 3) % 3;
        int r = 200 + band * 8;
        int g = 185 + band * 6;
        int b = 150 + band * 4;
        for (int x = 0; x < tileSize; ++x) {
            int n = static_cast<int>(hash3(x, y, 1, seed) % 9) - 4;
            setPixel(out, atlasSize, bx + x, by + y,
                     static_cast<uint8_t>(std::clamp(r + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(g + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(b + n, 0, 255)), 255);
        }
    }
}

void fillGravel(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    // Серый с крупным шумом — «камешки» разных оттенков.
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            int n = static_cast<int>(hash3(x, y, 2, seed) % 60) - 30;
            uint8_t v = static_cast<uint8_t>(std::clamp(125 + n, 0, 255));
            setPixel(out, atlasSize, bx + x, by + y, v, v, v, 255);
        }
    }
}

// ---------------------------------------------------------------------------
// tile 13–17: ores
// ---------------------------------------------------------------------------

// Общий шаблон для руды: базовый камень + редкие цветные «гнёзда» 2×2.
void fillOre(uint8_t* out, int atlasSize, int tileSize, int bx, int by,
             uint8_t r, uint8_t g, uint8_t b, uint32_t seed) {
    // 1. Базовый камень (как tile 3, но с собственным seed — не идентичен).
    fillNoisy(out, atlasSize, tileSize, bx, by, 130, 130, 130, 18, seed);

    // 2. Вкрапления: ~8 «гнёзд» по 2×2 пикселя.
    for (int i = 0; i < 8; ++i) {
        uint32_t h = hash3(i, 0, 0, seed + 900u + static_cast<uint32_t>(i));
        int px = static_cast<int>(h % static_cast<uint32_t>(tileSize));
        int py = static_cast<int>((h >> 8) % static_cast<uint32_t>(tileSize));

        for (int oy = 0; oy < 2; ++oy) {
            for (int ox = 0; ox < 2; ++ox) {
                int n = static_cast<int>(hash3(px + ox, py + oy, i, seed + 901u) % 21) - 10;
                setPixel(out, atlasSize, bx + px + ox, by + py + oy,
                         static_cast<uint8_t>(std::clamp(r + n, 0, 255)),
                         static_cast<uint8_t>(std::clamp(g + n, 0, 255)),
                         static_cast<uint8_t>(std::clamp(b + n, 0, 255)), 255);
            }
        }
    }
}

void fillCoalOre    (uint8_t* o, int a, int t, int bx, int by, uint32_t s) { fillOre(o, a, t, bx, by,  25,  25,  25, s); }
void fillIronOre    (uint8_t* o, int a, int t, int bx, int by, uint32_t s) { fillOre(o, a, t, bx, by, 200, 160, 120, s); }
void fillGoldOre    (uint8_t* o, int a, int t, int bx, int by, uint32_t s) { fillOre(o, a, t, bx, by, 240, 200,  50, s); }
void fillRedstoneOre(uint8_t* o, int a, int t, int bx, int by, uint32_t s) { fillOre(o, a, t, bx, by, 220,  40,  40, s); }
void fillDiamondOre (uint8_t* o, int a, int t, int bx, int by, uint32_t s) { fillOre(o, a, t, bx, by,  80, 230, 220, s); }

// ---------------------------------------------------------------------------
// tile 18: clay
// ---------------------------------------------------------------------------

void fillClay(uint8_t* out, int atlasSize, int tileSize, int bx, int by, uint32_t seed) {
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            int n = static_cast<int>(hash3(x, y, 0, seed) % 7) - 3;
            setPixel(out, atlasSize, bx + x, by + y,
                     static_cast<uint8_t>(std::clamp(150 + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(160 + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(175 + n, 0, 255)), 255);
        }
    }
}

// ---------------------------------------------------------------------------
// fallback: magenta/black checkerboard
// ---------------------------------------------------------------------------

void fillMissing(uint8_t* out, int atlasSize, int tileSize, int bx, int by) {
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            bool checker = ((x / 2) + (y / 2)) % 2 == 0;
            if (checker) setPixel(out, atlasSize, bx + x, by + y, 255, 0, 255, 255);
            else         setPixel(out, atlasSize, bx + x, by + y, 0, 0, 0, 255);
        }
    }
}

} // namespace

void buildDefaultAtlas(uint8_t* out, int atlasSize) {
    constexpr int TILES_PER_ROW = 16;   // синхронизировано с mesh_builder.cpp
    const int tileSize = atlasSize / TILES_PER_ROW;

    // Всё поле — fallback.
    for (int ty = 0; ty < TILES_PER_ROW; ++ty)
        for (int tx = 0; tx < TILES_PER_ROW; ++tx)
            fillMissing(out, atlasSize, tileSize, tx * tileSize, ty * tileSize);

    auto tileOrigin = [&](int idx, int& bx, int& by) {
        bx = (idx % TILES_PER_ROW) * tileSize;
        by = (idx / TILES_PER_ROW) * tileSize;
    };
    int bx, by;

    // 0–9: базовые блоки
    tileOrigin( 0, bx, by); fillNoisy    (out, atlasSize, tileSize, bx, by, 70, 150, 55, 18, 2001);  // grass top
    tileOrigin( 1, bx, by); fillGrassSide(out, atlasSize, tileSize, bx, by, 2002);                    // grass side
    tileOrigin( 2, bx, by); fillNoisy    (out, atlasSize, tileSize, bx, by, 120, 85, 55, 14, 2003);  // dirt
    tileOrigin( 3, bx, by); fillNoisy    (out, atlasSize, tileSize, bx, by, 130, 130, 130, 20, 2004); // stone
    tileOrigin( 4, bx, by); fillNoisy    (out, atlasSize, tileSize, bx, by, 210, 195, 150, 10, 2005); // sand
    tileOrigin( 5, bx, by); fillWoodSide (out, atlasSize, tileSize, bx, by, 2006);                    // wood side
    tileOrigin( 6, bx, by); fillWoodTop  (out, atlasSize, tileSize, bx, by, 2007);                    // wood top
    tileOrigin( 7, bx, by); fillLeaves   (out, atlasSize, tileSize, bx, by, 2008);                    // leaves
    tileOrigin( 8, bx, by); fillWater    (out, atlasSize, tileSize, bx, by, 2009);                    // water
    tileOrigin( 9, bx, by); fillNoisy    (out, atlasSize, tileSize, bx, by, 25, 25, 28, 6, 2010);     // bedrock

    // 10–12: snow / sandstone / gravel
    tileOrigin(10, bx, by); fillSnow     (out, atlasSize, tileSize, bx, by, 2011);   // snow
    tileOrigin(11, bx, by); fillSandstone(out, atlasSize, tileSize, bx, by, 2012);   // sandstone
    tileOrigin(12, bx, by); fillGravel   (out, atlasSize, tileSize, bx, by, 2013);   // gravel

    // 13–17: ores
    tileOrigin(13, bx, by); fillCoalOre    (out, atlasSize, tileSize, bx, by, 2014); // coal
    tileOrigin(14, bx, by); fillIronOre    (out, atlasSize, tileSize, bx, by, 2015); // iron
    tileOrigin(15, bx, by); fillGoldOre    (out, atlasSize, tileSize, bx, by, 2016); // gold
    tileOrigin(16, bx, by); fillRedstoneOre(out, atlasSize, tileSize, bx, by, 2017); // redstone
    tileOrigin(17, bx, by); fillDiamondOre (out, atlasSize, tileSize, bx, by, 2018); // diamond

    // 18: clay
    tileOrigin(18, bx, by); fillClay       (out, atlasSize, tileSize, bx, by, 2019); // clay
}

} // namespace mc
