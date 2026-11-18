#pragma once
#include "chunk.hpp"
#include "../core/random.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace mc {

class TerrainGenerator {
public:
    explicit TerrainGenerator(uint32_t seed) : seed(seed) {}

    // Внешний API: приблизительная высота поверхности (Alpha-стиль).
    int heightAt(int wx, int wz) const {
        return accurateSurfaceHeight(wx, wz);
    }

    void generate(Chunk& chunk) {
        const int baseX = chunk.chunkX() * CHUNK_SIZE;
        const int baseZ = chunk.chunkZ() * CHUNK_SIZE;

        HeightCache cache(baseX, baseZ);

        // 1) Основной рельеф через 3D-плотность (Alpha)
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                generateColumnAlpha(chunk, x, z, baseX + x, baseZ + z);
            }
        }

        // 2) Пещеры — Perlin worms (Alpha)
        carveWorms(chunk, baseX, baseZ);

        // 3) Руда по классическим уровням Alpha
        placeOresForChunk(chunk, baseX, baseZ);

        // 4) Деревья
        placeTreesForChunk(chunk, cache, baseX, baseZ);
    }

    // Alpha sea level = 64
    static constexpr int WATER_LEVEL = 64;

private:
    enum class Biome {
        Plains,
        Forest,
        Desert,
        Mountains,
        Beach,
        Ocean,
        Snowy
    };

    // -------------------------------------------------------------------------
    // Block IDs
    // -------------------------------------------------------------------------
    static constexpr BlockId AIR           = 0;
    static constexpr BlockId GRASS         = 1;
    static constexpr BlockId DIRT          = 2;
    static constexpr BlockId STONE         = 3;
    static constexpr BlockId SAND          = 4;
    static constexpr BlockId WOOD          = 5;
    static constexpr BlockId LEAVES        = 6;
    static constexpr BlockId WATER         = 7;
    static constexpr BlockId BEDROCK       = 8;
    static constexpr BlockId SNOW          = 9;
    static constexpr BlockId SANDSTONE     = 10;
    static constexpr BlockId GRAVEL        = 11;

    static constexpr BlockId COAL_ORE      = 12;
    static constexpr BlockId IRON_ORE      = 13;
    static constexpr BlockId GOLD_ORE      = 14;
    static constexpr BlockId REDSTONE_ORE  = 15;
    static constexpr BlockId DIAMOND_ORE   = 16;
    static constexpr BlockId CLAY          = 17;

    // -------------------------------------------------------------------------
    // Alpha-style параметры
    // -------------------------------------------------------------------------
    static constexpr float TERRAIN_XZ_SCALE = 0.045f;   // 1 блок ≈ 22 блока мира
    static constexpr float TERRAIN_Y_SCALE  = 0.0625f;  // 1/16
    static constexpr float TERRAIN_SLOPE_DIV = 10.0f;   // ширина перехода
    static constexpr float TERRAIN_NOISE_AMP = 1.0f;    // амплитуда нависаний

    // Perlin worms
    static constexpr int   WORM_CELL        = 32;   // сетка спавна червей
    static constexpr int   WORM_CHANCE      = 5;    // 1 из 5 ячеек
    static constexpr int   WORM_SEARCH      = 120;  // радиус поиска
    static constexpr int   WORM_MIN_LEN     = 60;
    static constexpr int   WORM_MAX_LEN     = 140;
    static constexpr float WORM_MIN_RADIUS  = 1.2f;
    static constexpr float WORM_MAX_RADIUS  = 2.4f;
    static constexpr float WORM_STEP        = 0.6f;
    static constexpr int   WORM_MIN_Y       = 6;
    static constexpr int   WORM_MAX_Y       = 60;

    // Деревья
    static constexpr int TREE_CELL  = 7;
    static constexpr int MAX_CANOPY = 3;
    static constexpr int SNOW_LINE  = 92;   // Alpha-высокие горы
    static constexpr int MOUNTAIN_LINE = 82;

    static constexpr int CACHE_MARGIN = MAX_CANOPY + TREE_CELL + 4;
    static constexpr int CACHE_SIZE   = CHUNK_SIZE + CACHE_MARGIN * 2;

    struct ColumnInfo {
        int height;
        Biome biome;
    };

    struct HeightCache {
        int baseX;
        int baseZ;
        std::array<ColumnInfo, CACHE_SIZE * CACHE_SIZE> data;

        HeightCache(int bx, int bz) : baseX(bx), baseZ(bz) {
            for (auto& v : data) {
                v.height = -1;
                v.biome = Biome::Plains;
            }
        }
    };

    // -------------------------------------------------------------------------
    // Utils
    // -------------------------------------------------------------------------
    static float clamp01(float v) {
        return std::max(0.0f, std::min(1.0f, v));
    }

    static float smoothstep(float edge0, float edge1, float x) {
        float t = clamp01((x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    static int floorDiv(int a, int b) {
        int q = a / b;
        int r = a % b;
        if (r < 0) --q;
        return q;
    }

    static int fastFloor(float v) {
        int i = static_cast<int>(v);
        return static_cast<float>(i) > v ? i - 1 : i;
    }

    static int cacheIndex(int wx, int wz, int baseX, int baseZ) {
        int lx = wx - baseX + CACHE_MARGIN;
        int lz = wz - baseZ + CACHE_MARGIN;
        if (lx < 0 || lx >= CACHE_SIZE || lz < 0 || lz >= CACHE_SIZE) return -1;
        return lz * CACHE_SIZE + lx;
    }

    ColumnInfo cachedColumn(HeightCache& cache, int wx, int wz) const {
        int idx = cacheIndex(wx, wz, cache.baseX, cache.baseZ);
        if (idx >= 0) {
            ColumnInfo& c = cache.data[idx];
            if (c.height < 0) {
                c.height = accurateSurfaceHeight(wx, wz);
                c.biome  = biomeAt(wx, wz, c.height);
            }
            return c;
        }
        int h = accurateSurfaceHeight(wx, wz);
        return ColumnInfo{h, biomeAt(wx, wz, h)};
    }

    // -------------------------------------------------------------------------
    // Noise
    // -------------------------------------------------------------------------
    float valueNoise2(float x, float z, uint32_t s) const {
        int xi = fastFloor(x);
        int zi = fastFloor(z);
        float xf = x - xi;
        float zf = z - zi;
        float u = xf * xf * (3.0f - 2.0f * xf);
        float v = zf * zf * (3.0f - 2.0f * zf);

        float a = noise3(xi,     0, zi,     s);
        float b = noise3(xi + 1, 0, zi,     s);
        float c = noise3(xi,     0, zi + 1, s);
        float d = noise3(xi + 1, 0, zi + 1, s);

        float top = a + (b - a) * u;
        float bot = c + (d - c) * u;
        return top + (bot - top) * v;
    }

    float valueNoise3(float x, float y, float z, uint32_t s) const {
        int xi = fastFloor(x);
        int yi = fastFloor(y);
        int zi = fastFloor(z);
        float xf = x - xi;
        float yf = y - yi;
        float zf = z - zi;
        float u = xf * xf * (3.0f - 2.0f * xf);
        float v = yf * yf * (3.0f - 2.0f * yf);
        float w = zf * zf * (3.0f - 2.0f * zf);

        float c000 = noise3(xi,     yi,     zi,     s);
        float c100 = noise3(xi + 1, yi,     zi,     s);
        float c010 = noise3(xi,     yi + 1, zi,     s);
        float c110 = noise3(xi + 1, yi + 1, zi,     s);
        float c001 = noise3(xi,     yi,     zi + 1, s);
        float c101 = noise3(xi + 1, yi,     zi + 1, s);
        float c011 = noise3(xi,     yi + 1, zi + 1, s);
        float c111 = noise3(xi + 1, yi + 1, zi + 1, s);

        float c00 = c000 + (c100 - c000) * u;
        float c10 = c010 + (c110 - c010) * u;
        float c01 = c001 + (c101 - c001) * u;
        float c11 = c011 + (c111 - c011) * u;
        float c0 = c00 + (c10 - c00) * v;
        float c1 = c01 + (c11 - c01) * v;
        return c0 + (c1 - c0) * w;
    }

    // -------------------------------------------------------------------------
    // Base height (2D) — «холмистость/континент»
    // -------------------------------------------------------------------------
    float computeBaseHeight(int wx, int wz) const {
        float continent = valueNoise2(wx * 0.0015f, wz * 0.0015f, seed + 100);
        // [0..1]; смещаем так, чтобы ~45% ниже sea level 64
        float base = 28.0f + continent * 78.0f;   // [28, 106], avg ~67

        float hills = valueNoise2(wx * 0.008f, wz * 0.008f, seed + 101);
        base += (hills - 0.5f) * 16.0f;

        // Горы (Alpha: высокие горы при высоком continent)
        float mountMask = smoothstep(0.55f, 0.80f, continent);
        if (mountMask > 0.0f) {
            float ridge = valueNoise2(wx * 0.020f, wz * 0.020f, seed + 102);
            ridge = 1.0f - std::fabs(ridge * 2.0f - 1.0f);
            ridge *= ridge;
            base += mountMask * (8.0f + ridge * 28.0f);
        }

        // Озёра
        float lakeNoise = valueNoise2(wx * 0.006f, wz * 0.006f, seed + 103);
        float lakeMask  = smoothstep(0.65f, 0.85f, lakeNoise);
        base -= lakeMask * 16.0f;

        if (base < 10.0f) base = 10.0f;
        if (base > CHUNK_HEIGHT - 20) base = CHUNK_HEIGHT - 20;
        return base;
    }

    // -------------------------------------------------------------------------
    // 3D-плотность (Alpha): > 0 — камень, < 0 — воздух
    // -------------------------------------------------------------------------
    float densityAt(int wx, int wy, int wz, float baseH) const {
        // Нависающие структуры — 2 октавы value-noise 3D
        float n1 = valueNoise3(wx * TERRAIN_XZ_SCALE,
                               wy * TERRAIN_Y_SCALE,
                               wz * TERRAIN_XZ_SCALE,
                               seed + 200);
        float n2 = valueNoise3(wx * TERRAIN_XZ_SCALE * 2.3f,
                               wy * TERRAIN_Y_SCALE * 1.8f,
                               wz * TERRAIN_XZ_SCALE * 2.3f,
                               seed + 201);

        float noiseVal = (n1 * 0.7f + n2 * 0.3f - 0.5f) * 2.0f;   // [-1, 1]

        // Наклон: положительный внутри земли, отрицательный выше
        float slope = (baseH - static_cast<float>(wy)) / TERRAIN_SLOPE_DIV;

        return slope + noiseVal * TERRAIN_NOISE_AMP;
    }

    int accurateSurfaceHeight(int wx, int wz) const {
        float baseH = computeBaseHeight(wx, wz);
        int startY = std::min(CHUNK_HEIGHT - 1, static_cast<int>(baseH) + 20);
        for (int y = startY; y >= 4; --y) {
            if (densityAt(wx, y, wz, baseH) > 0.0f) return y;
        }
        return 4;
    }

    // -------------------------------------------------------------------------
    // Biomes — температура + влажность (Alpha lookup)
    // -------------------------------------------------------------------------
    Biome biomeAt(int wx, int wz, int height) const {
        if (height < WATER_LEVEL - 1) return Biome::Ocean;
        if (height <= WATER_LEVEL + 1) return Biome::Beach;

        if (height >= SNOW_LINE) return Biome::Snowy;
        if (height >= MOUNTAIN_LINE) return Biome::Mountains;

        float temp  = valueNoise2(wx * 0.0040f, wz * 0.0040f, seed + 301);
        float humid = valueNoise2(wx * 0.0055f, wz * 0.0055f, seed + 302);

        // Hot + dry → пустыня
        if (temp > 0.60f && humid < 0.42f) return Biome::Desert;

        // Влажно → лес
        if (humid > 0.54f) return Biome::Forest;

        return Biome::Plains;
    }

    bool isMountainLike(Biome b) const {
        return b == Biome::Mountains || b == Biome::Snowy;
    }

    // -------------------------------------------------------------------------
    // Генерация колонки через 3D-плотность
    // -------------------------------------------------------------------------
    void generateColumnAlpha(Chunk& chunk, int lx, int lz, int wx, int wz) const {
        float baseH = computeBaseHeight(wx, wz);

        const int approx = static_cast<int>(baseH);
        const int minY   = std::max(0, approx - 20);
        const int maxY   = std::min(CHUNK_HEIGHT, approx + 20);

        int surfaceY = -1;

        // Бедрок
        chunk.set(lx, 0, lz, BEDROCK);
        for (int y = 1; y <= 3; ++y) {
            uint32_t h = hash3(wx, y, wz, seed + 520);
            int chance = 80 - y * 20;
            if (static_cast<int>(h % 100) < chance) {
                chunk.set(lx, y, lz, BEDROCK);
            } else {
                chunk.set(lx, y, lz, STONE);
            }
        }

        // Глубокий камень (ниже зоны перехода)
        for (int y = 4; y < minY; ++y) {
            chunk.set(lx, y, lz, STONE);
            surfaceY = y;
        }

        // 3D-плотность
        for (int y = minY; y < maxY; ++y) {
            float d = densityAt(wx, y, wz, baseH);
            if (d > 0.0f) {
                chunk.set(lx, y, lz, STONE);
                surfaceY = y;
            } else {
                chunk.set(lx, y, lz, AIR);
            }
        }

        // Всё выше — воздух
        for (int y = maxY; y < CHUNK_HEIGHT; ++y) {
            chunk.set(lx, y, lz, AIR);
        }

        // Поверхностные блоки
        if (surfaceY > 3 && surfaceY < CHUNK_HEIGHT) {
            applySurfaceRules(chunk, lx, lz, wx, wz, surfaceY);
        }

        // Вода (Alpha)
        if (surfaceY < WATER_LEVEL) {
            int waterEnd = std::min(WATER_LEVEL + 1, CHUNK_HEIGHT);
            for (int y = surfaceY + 1; y < waterEnd; ++y) {
                if (chunk.get(lx, y, lz) == AIR) {
                    chunk.set(lx, y, lz, WATER);
                }
            }
        }
    }

    void applySurfaceRules(Chunk& chunk, int lx, int lz, int wx, int wz, int surfaceY) const {
        const bool underwater = surfaceY <= WATER_LEVEL;
        Biome biome = biomeAt(wx, wz, surfaceY);

        BlockId topBlock = GRASS;
        BlockId fillBlock = DIRT;

        switch (biome) {
            case Biome::Desert:
            case Biome::Beach:
            case Biome::Ocean:
                topBlock = SAND;
                fillBlock = SAND;
                break;
            case Biome::Mountains:
                topBlock = STONE;
                fillBlock = STONE;
                break;
            case Biome::Snowy:
                topBlock = SNOW;
                fillBlock = DIRT;
                break;
            case Biome::Forest:
            case Biome::Plains:
            default:
                break;
        }

        if (underwater) {
            topBlock = SAND;
            fillBlock = SAND;

            float patch = valueNoise2(wx * 0.08f, wz * 0.08f, seed + 510);
            if (patch > 0.72f) topBlock = CLAY;
            else if (patch < 0.18f) topBlock = GRAVEL;
        }

        if (isMountainLike(biome) && surfaceY > WATER_LEVEL + 4) {
            float exposed = valueNoise2(wx * 0.09f, wz * 0.09f, seed + 530);
            if (exposed > 0.58f) topBlock = STONE;
        }

        chunk.set(lx, surfaceY, lz, topBlock);

        for (int dy = 1; dy <= 4; ++dy) {
            int y = surfaceY - dy;
            if (y <= 3) break;
            if (chunk.get(lx, y, lz) == STONE) {
                chunk.set(lx, y, lz, fillBlock);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Perlin worms (Alpha-пещеры)
    // -------------------------------------------------------------------------
    void carveWorms(Chunk& chunk, int baseX, int baseZ) const {
        int cellMinX = floorDiv(baseX - WORM_SEARCH, WORM_CELL);
        int cellMaxX = floorDiv(baseX + CHUNK_SIZE + WORM_SEARCH, WORM_CELL);
        int cellMinZ = floorDiv(baseZ - WORM_SEARCH, WORM_CELL);
        int cellMaxZ = floorDiv(baseZ + CHUNK_SIZE + WORM_SEARCH, WORM_CELL);

        for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
            for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
                uint32_t h = hash3(cx, 0, cz, seed + 400);
                if (static_cast<int>(h % WORM_CHANCE) != 0) continue;

                // Начальная позиция
                float wx = static_cast<float>(cx * WORM_CELL)
                         + static_cast<float>((h >> 0) & 0xFF) * WORM_CELL / 256.0f;
                float wz = static_cast<float>(cz * WORM_CELL)
                         + static_cast<float>((h >> 8) & 0xFF) * WORM_CELL / 256.0f;
                float wy = static_cast<float>(WORM_MIN_Y)
                         + static_cast<float>((h >> 16) & 0x3F) / 63.0f
                         * static_cast<float>(WORM_MAX_Y - WORM_MIN_Y);

                // Направление
                float dx = static_cast<float>((h >> 22) & 0x3FF) / 511.5f - 1.0f;
                float dy = static_cast<float>((h >> 12) & 0x3FF) / 511.5f - 1.0f;
                float dz = static_cast<float>((h >> 2)  & 0x3FF) / 511.5f - 1.0f;
                float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (len < 1e-4f) { dx = 1.0f; dy = 0.0f; dz = 0.0f; len = 1.0f; }
                dx /= len; dy = dy / len * 0.35f; dz /= len;
                len = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= len; dy /= len; dz /= len;

                int wormLen = WORM_MIN_LEN + static_cast<int>((h >> 8) & 0x7F) % (WORM_MAX_LEN - WORM_MIN_LEN);
                float baseR = WORM_MIN_RADIUS
                            + static_cast<float>((h >> 24) & 0xF) / 15.0f
                            * (WORM_MAX_RADIUS - WORM_MIN_RADIUS);

                // Толщина варьируется по длине
                uint32_t saltA = seed + 401;
                uint32_t saltB = seed + 402;

                for (int step = 0; step < wormLen; ++step) {
                    float t = static_cast<float>(step) / static_cast<float>(wormLen);
                    float radius = baseR * (0.7f + 0.5f * std::sin(t * 6.2831f * 2.0f));

                    carveWormSphere(chunk, baseX, baseZ, wx, wy, wz, radius);

                    // Возмущение направления (Perlin)
                    float p1 = valueNoise3(static_cast<float>(step) * 0.08f, 0.0f, 0.0f, saltA);
                    float p2 = valueNoise3(static_cast<float>(step) * 0.08f, 11.0f, 0.0f, saltA);
                    float p3 = valueNoise3(static_cast<float>(step) * 0.08f, 22.0f, 0.0f, saltB);

                    dx += (p1 - 0.5f) * 0.40f;
                    dy += (p2 - 0.5f) * 0.25f;
                    dz += (p3 - 0.5f) * 0.40f;

                    float l2 = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (l2 > 1e-4f) { dx /= l2; dy /= l2; dz /= l2; }

                    wx += dx * WORM_STEP;
                    wy += dy * WORM_STEP;
                    wz += dz * WORM_STEP;

                    if (wy < static_cast<float>(WORM_MIN_Y)) wy = static_cast<float>(WORM_MIN_Y);
                    if (wy > static_cast<float>(WORM_MAX_Y)) wy = static_cast<float>(WORM_MAX_Y);
                }
            }
        }
    }

    void carveWormSphere(Chunk& chunk, int baseX, int baseZ,
                         float fwx, float fwy, float fwz,
                         float radius) const {
        int cx = static_cast<int>(std::floor(fwx));
        int cy = static_cast<int>(std::floor(fwy));
        int cz = static_cast<int>(std::floor(fwz));
        int r  = static_cast<int>(std::ceil(radius));
        int r2 = r * r;

        for (int dy = -r; dy <= r; ++dy) {
            int y = cy + dy;
            if (y < 4 || y >= CHUNK_HEIGHT) continue;

            for (int dz = -r; dz <= r; ++dz) {
                int lz = cz + dz - baseZ;
                if (lz < 0 || lz >= CHUNK_SIZE) continue;

                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy + dz * dz > r2) continue;

                    int lx = cx + dx - baseX;
                    if (lx < 0 || lx >= CHUNK_SIZE) continue;

                    BlockId id = chunk.get(lx, y, lz);
                    if (id == STONE || id == DIRT || id == GRASS ||
                        id == SAND || id == SANDSTONE || id == GRAVEL ||
                        id == COAL_ORE || id == IRON_ORE || id == GOLD_ORE ||
                        id == REDSTONE_ORE || id == DIAMOND_ORE || id == CLAY) {
                        chunk.set(lx, y, lz, AIR);
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Fast ores (Alpha levels)
    // -------------------------------------------------------------------------
    struct OreRule {
        BlockId id;
        uint32_t salt;
        int cellSize;
        int minY;
        int maxY;
        int triesPerCell;
        int radius;
    };

    void placeOresForChunk(Chunk& chunk, int baseX, int baseZ) const {
        // Уровни как в Minecraft Alpha
        static constexpr OreRule rules[] = {
            { COAL_ORE,     710, 8,  5, 100, 2, 2 },
            { IRON_ORE,     711, 8,  5,  64, 1, 2 },
            { GOLD_ORE,     712, 9,  5,  32, 1, 1 },
            { REDSTONE_ORE, 713, 8,  5,  16, 1, 2 },
            { DIAMOND_ORE,  714, 10, 5,  16, 1, 1 }
        };

        for (const OreRule& rule : rules) {
            placeOreRule(chunk, baseX, baseZ, rule);
        }
    }

    void placeOreRule(Chunk& chunk, int baseX, int baseZ, const OreRule& rule) const {
        int minX = baseX - rule.radius;
        int maxX = baseX + CHUNK_SIZE - 1 + rule.radius;
        int minZ = baseZ - rule.radius;
        int maxZ = baseZ + CHUNK_SIZE - 1 + rule.radius;

        int cellMinX = floorDiv(minX, rule.cellSize);
        int cellMaxX = floorDiv(maxX, rule.cellSize);
        int cellMinZ = floorDiv(minZ, rule.cellSize);
        int cellMaxZ = floorDiv(maxZ, rule.cellSize);

        int yMinCell = floorDiv(rule.minY, rule.cellSize);
        int yMaxCell = floorDiv(rule.maxY, rule.cellSize);

        for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
            for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
                for (int cy = yMinCell; cy <= yMaxCell; ++cy) {
                    for (int attempt = 0; attempt < rule.triesPerCell; ++attempt) {
                        uint32_t ah = hash3(cx * 31 + attempt, cy * 17, cz * 13,
                                            seed + rule.salt + 1000u);

                        int ox = static_cast<int>((ah >> 0)  & 0xFF) * rule.cellSize / 256;
                        int oy = static_cast<int>((ah >> 8)  & 0xFF) * rule.cellSize / 256;
                        int oz = static_cast<int>((ah >> 16) & 0xFF) * rule.cellSize / 256;

                        int wx = cx * rule.cellSize + ox;
                        int wy = cy * rule.cellSize + oy;
                        int wz = cz * rule.cellSize + oz;

                        if (wy < rule.minY || wy > rule.maxY) continue;

                        int lx = wx - baseX;
                        int lz = wz - baseZ;

                        if (lx < -rule.radius || lx >= CHUNK_SIZE + rule.radius) continue;
                        if (lz < -rule.radius || lz >= CHUNK_SIZE + rule.radius) continue;

                        // Грубая отсечка: не выше поверхности
                        float baseH = computeBaseHeight(wx, wz);
                        if (wy >= baseH + 4) continue;

                        placeOreBlob(chunk, baseX, baseZ, lx, wy, lz,
                                     rule.id, rule.radius, ah);
                    }
                }
            }
        }
    }

    void placeOreBlob(Chunk& chunk, int baseX, int baseZ,
                      int centerX, int centerY, int centerZ,
                      BlockId ore, int radius, uint32_t h) const {
        int dxLen = radius;
        int dzLen = radius;
        int dyLen = std::max(1, radius - 1);

        if ((h & 1u) != 0u) ++dxLen;
        if ((h & 2u) != 0u) ++dzLen;

        for (int dy = -dyLen; dy <= dyLen; ++dy) {
            int y = centerY + dy;
            if (y <= 3 || y >= CHUNK_HEIGHT) continue;

            for (int dz = -dzLen; dz <= dzLen; ++dz) {
                int localZ = centerZ + dz;
                if (localZ < 0 || localZ >= CHUNK_SIZE) continue;

                for (int dx = -dxLen; dx <= dxLen; ++dx) {
                    int localX = centerX + dx;
                    if (localX < 0 || localX >= CHUNK_SIZE) continue;

                    float nx = static_cast<float>(dx) / std::max(1, dxLen);
                    float ny = static_cast<float>(dy) / std::max(1, dyLen);
                    float nz = static_cast<float>(dz) / std::max(1, dzLen);
                    if (nx * nx + ny * ny + nz * nz > 1.0f) continue;

                    int wx = baseX + localX;
                    int wz = baseZ + localZ;

                    uint32_t rh = hash3(wx, y, wz, seed + 800u);
                    if ((rh & 7u) == 0u) continue;

                    if (chunk.get(localX, y, localZ) == STONE) {
                        chunk.set(localX, y, localZ, ore);
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Trees (Alpha-style: плотный лес, редкие равнины)
    // -------------------------------------------------------------------------
    bool treeCandidate(int cellX, int cellZ,
                       HeightCache& cache,
                       int& outX, int& outZ, int& outSurf,
                       Biome& outBiome) const {
        uint32_t h = hash3(cellX, 3, cellZ, seed + 600);

        float r  = static_cast<float>(h & 0xFFFFu) / 65535.0f;
        float jx = static_cast<float>((h >> 16) & 0xFF) / 255.0f;
        float jz = static_cast<float>((h >> 24) & 0xFF) / 255.0f;

        int px = cellX * TREE_CELL + static_cast<int>(jx * TREE_CELL);
        int pz = cellZ * TREE_CELL + static_cast<int>(jz * TREE_CELL);

        ColumnInfo info = cachedColumn(cache, px, pz);
        int surf = info.height;
        Biome biome = info.biome;

        float prob = 0.0f;
        switch (biome) {
            case Biome::Forest:    prob = 0.80f; break;
            case Biome::Plains:    prob = 0.08f; break;
            case Biome::Mountains: prob = 0.03f; break;
            case Biome::Snowy:     prob = 0.02f; break;
            default:               prob = 0.0f;  break;
        }

        if (r > prob) return false;
        if (surf <= WATER_LEVEL + 1) return false;
        if (surf >= CHUNK_HEIGHT - 12) return false;

        outX = px; outZ = pz; outSurf = surf; outBiome = biome;
        return true;
    }

    bool inChunk(int lx, int ly, int lz) const {
        return lx >= 0 && lx < CHUNK_SIZE &&
               ly > 0 && ly < CHUNK_HEIGHT &&
               lz >= 0 && lz < CHUNK_SIZE;
    }

    void setIfAirOrLeaves(Chunk& chunk, int lx, int ly, int lz, BlockId id) const {
        if (!inChunk(lx, ly, lz)) return;
        BlockId e = chunk.get(lx, ly, lz);
        if (e == AIR || e == LEAVES) chunk.set(lx, ly, lz, id);
    }

    void setLeavesIfAir(Chunk& chunk, int lx, int ly, int lz) const {
        if (!inChunk(lx, ly, lz)) return;
        if (chunk.get(lx, ly, lz) == AIR) chunk.set(lx, ly, lz, LEAVES);
    }

    void placeTree(Chunk& chunk, int lx, int ly, int lz,
                   int worldX, int worldZ, Biome biome) const {
        uint32_t th = hash3(worldX, 5, worldZ, seed + 620);
        int trunkH = 4 + static_cast<int>(th % 3);

        if (biome == Biome::Forest && ((th >> 8) & 7u) == 0u) ++trunkH;

        for (int dy = 0; dy < trunkH; ++dy) {
            setIfAirOrLeaves(chunk, lx, ly + dy, lz, WOOD);
        }

        int crownBase = ly + trunkH - 2;

        for (int dy = 0; dy <= 3; ++dy) {
            int py = crownBase + dy;
            int radius = (dy <= 1) ? 2 : 1;

            for (int dx = -radius; dx <= radius; ++dx) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    int ax = std::abs(dx);
                    int az = std::abs(dz);

                    if (radius == 2 && ax == 2 && az == 2) {
                        uint32_t ch = hash3(worldX + dx, py, worldZ + dz, seed + 621);
                        if ((ch & 3u) != 0u) continue;
                    }

                    if (dy == 3 && ax + az > 1) continue;
                    if (dx == 0 && dz == 0 && py < ly + trunkH) continue;

                    setLeavesIfAir(chunk, lx + dx, py, lz + dz);
                }
            }
        }

        setIfAirOrLeaves(chunk, lx, ly + trunkH - 1, lz, WOOD);
    }

    void placeTreesForChunk(Chunk& chunk, HeightCache& cache,
                            int baseX, int baseZ) const {
        int minX = baseX - MAX_CANOPY;
        int maxX = baseX + CHUNK_SIZE - 1 + MAX_CANOPY;
        int minZ = baseZ - MAX_CANOPY;
        int maxZ = baseZ + CHUNK_SIZE - 1 + MAX_CANOPY;

        int cellMinX = floorDiv(minX, TREE_CELL);
        int cellMaxX = floorDiv(maxX, TREE_CELL);
        int cellMinZ = floorDiv(minZ, TREE_CELL);
        int cellMaxZ = floorDiv(maxZ, TREE_CELL);

        for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
            for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
                int tx = 0, tz = 0, ty = 0;
                Biome biome = Biome::Plains;

                if (!treeCandidate(cx, cz, cache, tx, tz, ty, biome)) continue;

                placeTree(chunk, tx - baseX, ty, tz - baseZ, tx, tz, biome);
            }
        }
    }

    uint32_t seed;
};

} // namespace mc
