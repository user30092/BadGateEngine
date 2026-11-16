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

    int heightAt(int wx, int wz) const {
        return heightAtUncached(wx, wz);
    }

    void generate(Chunk& chunk) {
        const int baseX = chunk.chunkX() * CHUNK_SIZE;
        const int baseZ = chunk.chunkZ() * CHUNK_SIZE;

        // Кэш высот/биомов с запасом под деревья из соседних чанков.
        HeightCache cache(baseX, baseZ);

        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                const int wx = baseX + x;
                const int wz = baseZ + z;

                const ColumnInfo info = cachedColumn(cache, wx, wz);

                generateColumnFast(chunk, x, z, wx, wz, info.height, info.biome);
            }
        }

        placeOresForChunk(chunk, baseX, baseZ);
        placeTreesForChunk(chunk, cache, baseX, baseZ);
    }

    static constexpr int WATER_LEVEL = 32;

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
    // Performance knobs
    // -------------------------------------------------------------------------

    // Чем больше значение, тем быстрее, но тем грубее пещеры.
    // Для слабого Android/старого ПК можно поставить 5 или 6.
    static constexpr int CAVE_STEP_XZ = 4;
    static constexpr int CAVE_STEP_Y  = 4;

    static constexpr int TREE_CELL  = 7;
    static constexpr int MAX_CANOPY = 3;
    static constexpr int SNOW_LINE  = 78;

    static constexpr int CACHE_MARGIN = MAX_CANOPY + TREE_CELL + 4;
    static constexpr int CACHE_SIZE = CHUNK_SIZE + CACHE_MARGIN * 2;

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

        if (lx < 0 || lx >= CACHE_SIZE || lz < 0 || lz >= CACHE_SIZE) {
            return -1;
        }

        return lz * CACHE_SIZE + lx;
    }

    ColumnInfo cachedColumn(HeightCache& cache, int wx, int wz) const {
        int idx = cacheIndex(wx, wz, cache.baseX, cache.baseZ);

        if (idx >= 0) {
            ColumnInfo& c = cache.data[idx];

            if (c.height < 0) {
                int h = heightAtUncached(wx, wz);
                c.height = h;
                c.biome = biomeAt(wx, wz, h);
            }

            return c;
        }

        int h = heightAtUncached(wx, wz);
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

    float fbm2(float x, float z, uint32_t s, int octaves, float persistence) const {
        float total = 0.0f;
        float amp = 1.0f;
        float maxAmp = 0.0f;

        float fx = x;
        float fz = z;

        for (int i = 0; i < octaves; ++i) {
            total += valueNoise2(fx, fz, s + static_cast<uint32_t>(i) * 131u) * amp;
            maxAmp += amp;

            amp *= persistence;
            fx *= 2.0f;
            fz *= 2.0f;
        }

        return total / maxAmp;
    }

    // -------------------------------------------------------------------------
    // Height / biome
    // -------------------------------------------------------------------------

    int heightAtUncached(int wx, int wz) const {
        // Уменьшил количество октав относительно предыдущей версии.
        float continent = fbm2(wx * 0.0022f, wz * 0.0022f, seed + 11, 3, 0.52f);
        float hills     = fbm2(wx * 0.0100f, wz * 0.0100f, seed + 12, 3, 0.50f);
        float rough     = valueNoise2(wx * 0.0350f, wz * 0.0350f, seed + 13);

        float mountainNoise = fbm2(wx * 0.0060f, wz * 0.0060f, seed + 14, 3, 0.50f);
        float mountainMask = smoothstep(0.50f, 0.78f, mountainNoise);

        float ridgeBase = valueNoise2(wx * 0.0180f, wz * 0.0180f, seed + 15);
        float ridge = 1.0f - std::fabs(ridgeBase * 2.0f - 1.0f);
        ridge *= ridge;

        float lakeNoise = valueNoise2(wx * 0.0075f, wz * 0.0075f, seed + 16);
        float lakeMask = smoothstep(0.60f, 0.82f, lakeNoise);

        float h = 18.0f;

        h += (continent - 0.45f) * 38.0f;
        h += (hills - 0.45f) * 24.0f;
        h += (rough - 0.5f) * 7.0f;
        h += mountainMask * (18.0f + ridge * 40.0f);
        h -= lakeMask * 12.0f;

        if (mountainMask > 0.55f && rough > 0.58f) {
            h = std::floor(h * 0.5f) * 2.0f;
        }

        int result = static_cast<int>(h);

        if (result < 4) result = 4;
        if (result >= CHUNK_HEIGHT - 8) result = CHUNK_HEIGHT - 8;

        return result;
    }

    Biome biomeAt(int wx, int wz, int height) const {
        if (height < WATER_LEVEL - 1) {
            return Biome::Ocean;
        }

        if (height <= WATER_LEVEL + 1) {
            return Biome::Beach;
        }

        if (height > SNOW_LINE) {
            return Biome::Snowy;
        }

        float temp = valueNoise2(wx * 0.0040f, wz * 0.0040f, seed + 201);
        float humid = valueNoise2(wx * 0.0055f, wz * 0.0055f, seed + 202);
        float rock = valueNoise2(wx * 0.0080f, wz * 0.0080f, seed + 203);

        if (height > 62 && rock > 0.48f) {
            return Biome::Mountains;
        }

        if (temp > 0.62f && humid < 0.42f) {
            return Biome::Desert;
        }

        if (humid > 0.54f) {
            return Biome::Forest;
        }

        return Biome::Plains;
    }

    bool isMountainLike(Biome b) const {
        return b == Biome::Mountains || b == Biome::Snowy;
    }

    // -------------------------------------------------------------------------
    // Fast caves
    // -------------------------------------------------------------------------

    bool cavePoint(int gx, int gy, int gz) const {
        // gx/gy/gz — координаты грубой сетки, не мировые блоки.
        // Переводим в мир.
        int wx = gx * CAVE_STEP_XZ;
        int wy = gy * CAVE_STEP_Y;
        int wz = gz * CAVE_STEP_XZ;

        if (wy <= 4) return false;

        float n1 = valueNoise3(
            wx * 0.035f,
            wy * 0.050f,
            wz * 0.035f,
            seed + 301
        );

        float n2 = valueNoise3(
            wx * 0.035f + 41.7f,
            wy * 0.050f - 19.3f,
            wz * 0.035f + 73.1f,
            seed + 302
        );

        bool spaghetti =
            std::fabs(n1 - 0.5f) < 0.040f &&
            std::fabs(n2 - 0.5f) < 0.040f;

        bool cavern = false;

        if (wy < 58) {
            float cheese = valueNoise3(
                wx * 0.018f,
                wy * 0.026f,
                wz * 0.018f,
                seed + 303
            );

            cavern = cheese > 0.735f;
        }

        return spaghetti || cavern;
    }

    void carveCavesFast(Chunk& chunk,
                        int lx,
                        int lz,
                        int wx,
                        int wz,
                        int surf) const {
        if (surf <= 8) return;

        const int maxY = std::min(surf - 5, CHUNK_HEIGHT - 1);

        if (maxY <= 5) return;

        const int gx = floorDiv(wx, CAVE_STEP_XZ);
        const int gz = floorDiv(wz, CAVE_STEP_XZ);

        const int rx = wx - gx * CAVE_STEP_XZ;
        const int rz = wz - gz * CAVE_STEP_XZ;

        // Быстрый bilinear по 4 углам cave-cell.
        const float tx = static_cast<float>(rx) / CAVE_STEP_XZ;
        const float tz = static_cast<float>(rz) / CAVE_STEP_XZ;

        const float ix0 = 1.0f - tx;
        const float ix1 = tx;
        const float iz0 = 1.0f - tz;
        const float iz1 = tz;

        const float w00 = ix0 * iz0;
        const float w10 = ix1 * iz0;
        const float w01 = ix0 * iz1;
        const float w11 = ix1 * iz1;

        int gy0 = 5 / CAVE_STEP_Y;
        int gy1 = maxY / CAVE_STEP_Y + 1;

        for (int gy = gy0; gy <= gy1; ++gy) {
            int y0 = gy * CAVE_STEP_Y;
            int y1 = y0 + CAVE_STEP_Y - 1;

            int fromY = std::max(5, y0);
            int toY = std::min(maxY, y1);

            if (fromY > toY) continue;

            bool c00 = cavePoint(gx,     gy, gz);
            bool c10 = cavePoint(gx + 1, gy, gz);
            bool c01 = cavePoint(gx,     gy, gz + 1);
            bool c11 = cavePoint(gx + 1, gy, gz + 1);

            float density =
                (c00 ? w00 : 0.0f) +
                (c10 ? w10 : 0.0f) +
                (c01 ? w01 : 0.0f) +
                (c11 ? w11 : 0.0f);

            // Порог влияет на толщину пещер.
            // 0.45 — больше пещер.
            // 0.60 — меньше пещер и быстрее визуально чище.
            if (density < 0.52f) continue;

            for (int y = fromY; y <= toY; ++y) {
                BlockId id = chunk.get(lx, y, lz);

                if (id == STONE ||
                    id == DIRT ||
                    id == SAND ||
                    id == SANDSTONE ||
                    id == GRAVEL ||
                    id == COAL_ORE ||
                    id == IRON_ORE ||
                    id == GOLD_ORE ||
                    id == REDSTONE_ORE ||
                    id == DIAMOND_ORE ||
                    id == CLAY) {
                    chunk.set(lx, y, lz, AIR);
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Column generation
    // -------------------------------------------------------------------------

    void generateColumnFast(Chunk& chunk,
                            int lx,
                            int lz,
                            int wx,
                            int wz,
                            int surf,
                            Biome biome) const {
        const bool underwater = surf <= WATER_LEVEL;

        BlockId topBlock = GRASS;
        BlockId fillBlock = DIRT;
        BlockId deepFillBlock = STONE;

        switch (biome) {
            case Biome::Desert:
            case Biome::Beach:
            case Biome::Ocean:
                topBlock = SAND;
                fillBlock = SAND;
                deepFillBlock = SANDSTONE;
                break;

            case Biome::Mountains:
                topBlock = STONE;
                fillBlock = STONE;
                deepFillBlock = STONE;
                break;

            case Biome::Snowy:
                topBlock = SNOW;
                fillBlock = DIRT;
                deepFillBlock = STONE;
                break;

            case Biome::Forest:
            case Biome::Plains:
            default:
                topBlock = GRASS;
                fillBlock = DIRT;
                deepFillBlock = STONE;
                break;
        }

        // Сначала воздух.
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            chunk.set(lx, y, lz, AIR);
        }

        // Бедрок.
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

        // Глубокий камень.
        int deepEnd = std::min(surf - 8, CHUNK_HEIGHT);
        for (int y = 4; y < deepEnd; ++y) {
            chunk.set(lx, y, lz, STONE);
        }

        // Средний слой.
        int midStart = std::max(4, surf - 8);
        int midEnd = std::min(surf - 4, CHUNK_HEIGHT);

        for (int y = midStart; y < midEnd; ++y) {
            if (deepFillBlock == STONE) {
                chunk.set(lx, y, lz, STONE);
            } else {
                chunk.set(lx, y, lz, deepFillBlock);
            }
        }

        // Верхний fill.
        int fillStart = std::max(4, surf - 4);
        int fillEnd = std::min(surf - 1, CHUNK_HEIGHT);

        for (int y = fillStart; y < fillEnd; ++y) {
            chunk.set(lx, y, lz, fillBlock);
        }

        // Top block.
        if (surf - 1 > 3 && surf - 1 < CHUNK_HEIGHT) {
            BlockId top = underwater ? SAND : topBlock;

            if (underwater && surf >= WATER_LEVEL - 4) {
                float shorePatch = valueNoise2(wx * 0.08f, wz * 0.08f, seed + 510);

                if (shorePatch > 0.72f) {
                    top = CLAY;
                } else if (shorePatch < 0.18f) {
                    top = GRAVEL;
                }
            }

            if (isMountainLike(biome)) {
                float exposedRock = valueNoise2(wx * 0.09f, wz * 0.09f, seed + 530);

                if (exposedRock > 0.58f && surf > WATER_LEVEL + 4) {
                    top = STONE;
                }
            }

            chunk.set(lx, surf - 1, lz, top);
        }

        // Вода.
        if (underwater) {
            int waterStart = std::max(surf, 4);
            int waterEnd = std::min(WATER_LEVEL + 1, CHUNK_HEIGHT);

            for (int y = waterStart; y < waterEnd; ++y) {
                chunk.set(lx, y, lz, WATER);
            }
        }

        // Гравийные карманы дешёво: не на каждый блок, а редкими пятнами.
        addGravelPockets(chunk, lx, lz, wx, wz, surf);

        // Пещеры после заполнения столба.
        carveCavesFast(chunk, lx, lz, wx, wz, surf);
    }

    void addGravelPockets(Chunk& chunk,
                          int lx,
                          int lz,
                          int wx,
                          int wz,
                          int surf) const {
        if (surf < 16) return;

        // Проверяем только несколько глубин, а не весь столб.
        float patch = valueNoise2(wx * 0.045f, wz * 0.045f, seed + 501);

        if (patch < 0.76f) return;

        int centerY = 8 + static_cast<int>(hash3(wx, 0, wz, seed + 502) % static_cast<uint32_t>(std::max(1, surf - 16)));
        int radius = 2;

        int fromY = std::max(5, centerY - radius);
        int toY = std::min(surf - 6, centerY + radius);

        for (int y = fromY; y <= toY; ++y) {
            if (chunk.get(lx, y, lz) == STONE) {
                chunk.set(lx, y, lz, GRAVEL);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Fast ores
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
        static constexpr OreRule rules[] = {
            { COAL_ORE,     710, 8, 8, 72, 2, 2 },
            { IRON_ORE,     711, 8, 5, 58, 1, 2 },
            { GOLD_ORE,     712, 9, 4, 32, 1, 1 },
            { REDSTONE_ORE, 713, 8, 3, 18, 1, 2 },
            { DIAMOND_ORE,  714, 10, 3, 16, 1, 1 }
        };

        for (const OreRule& rule : rules) {
            placeOreRule(chunk, baseX, baseZ, rule);
        }
    }

    void placeOreRule(Chunk& chunk,
                      int baseX,
                      int baseZ,
                      const OreRule& rule) const {
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
                    uint32_t h = hash3(cx, cy, cz, seed + rule.salt);

                    // Несколько попыток на клетку.
                    for (int attempt = 0; attempt < rule.triesPerCell; ++attempt) {
                        uint32_t ah = hash3(cx * 31 + attempt, cy * 17, cz * 13, seed + rule.salt + 1000u);

                        int ox = static_cast<int>((ah >> 0)  & 0xFF) * rule.cellSize / 256;
                        int oy = static_cast<int>((ah >> 8)  & 0xFF) * rule.cellSize / 256;
                        int oz = static_cast<int>((ah >> 16) & 0xFF) * rule.cellSize / 256;

                        int wx = cx * rule.cellSize + ox;
                        int wy = cy * rule.cellSize + oy;
                        int wz = cz * rule.cellSize + oz;

                        if (wy < rule.minY || wy > rule.maxY) continue;

                        int lx = wx - baseX;
                        int lz = wz - baseZ;

                        // Центр может быть за чанком, но жила должна залезть внутрь.
                        if (lx < -rule.radius || lx >= CHUNK_SIZE + rule.radius) continue;
                        if (lz < -rule.radius || lz >= CHUNK_SIZE + rule.radius) continue;

                        int surf = heightAtUncached(wx, wz);
                        if (wy >= surf - 4) continue;

                        placeOreBlob(chunk, baseX, baseZ, lx, wy, lz, rule.id, rule.radius, ah);
                    }
                }
            }
        }
    }

    void placeOreBlob(Chunk& chunk,
                      int baseX,
                      int baseZ,
                      int centerX,
                      int centerY,
                      int centerZ,
                      BlockId ore,
                      int radius,
                      uint32_t h) const {
        // Маленькая вытянутая жила.
        int dxLen = radius;
        int dzLen = radius;
        int dyLen = std::max(1, radius - 1);

        if ((h & 1u) != 0u) dxLen += 1;
        if ((h & 2u) != 0u) dzLen += 1;

        for (int dy = -dyLen; dy <= dyLen; ++dy) {
            int y = centerY + dy;
            if (y <= 3 || y >= CHUNK_HEIGHT) continue;

            for (int dz = -dzLen; dz <= dzLen; ++dz) {
                int lx = centerX + dz;
                if (lx < 0 || lx >= CHUNK_SIZE) continue;

                for (int dx = -dxLen; dx <= dxLen; ++dx) {
                    int localX = centerX + dx;
                    if (localX < 0 || localX >= CHUNK_SIZE) continue;

                    // Ошибка в именах выше не страшна, но лучше явно:
                    // centerX — local X центра, centerZ — local Z центра.
                    int localZ = centerZ + dz;
                    if (localZ < 0 || localZ >= CHUNK_SIZE) continue;

                    float nx = static_cast<float>(dx) / std::max(1, dxLen);
                    float ny = static_cast<float>(dy) / std::max(1, dyLen);
                    float nz = static_cast<float>(dz) / std::max(1, dzLen);

                    if (nx * nx + ny * ny + nz * nz > 1.0f) continue;

                    int wx = baseX + localX;
                    int wz = baseZ + localZ;

                    // Небольшая рваность.
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
    // Trees
    // -------------------------------------------------------------------------

    bool treeCandidate(int cellX,
                       int cellZ,
                       HeightCache& cache,
                       int& outX,
                       int& outZ,
                       int& outSurf,
                       Biome& outBiome) const {
        uint32_t h = hash3(cellX, 3, cellZ, seed + 600);

        float r = static_cast<float>(h & 0xFFFFu) / 65535.0f;
        float jx = static_cast<float>((h >> 16) & 0xFF) / 255.0f;
        float jz = static_cast<float>((h >> 24) & 0xFF) / 255.0f;

        int px = cellX * TREE_CELL + static_cast<int>(jx * TREE_CELL);
        int pz = cellZ * TREE_CELL + static_cast<int>(jz * TREE_CELL);

        ColumnInfo info = cachedColumn(cache, px, pz);

        int surf = info.height;
        Biome biome = info.biome;

        float prob = 0.0f;

        switch (biome) {
            case Biome::Forest:
                prob = 0.72f;
                break;
            case Biome::Plains:
                prob = 0.10f;
                break;
            case Biome::Mountains:
                prob = 0.04f;
                break;
            case Biome::Snowy:
                prob = 0.02f;
                break;
            default:
                prob = 0.0f;
                break;
        }

        if (r > prob) return false;
        if (surf <= WATER_LEVEL + 1) return false;
        if (surf >= CHUNK_HEIGHT - 12) return false;

        outX = px;
        outZ = pz;
        outSurf = surf;
        outBiome = biome;

        return true;
    }

    bool inChunk(int lx, int ly, int lz) const {
        return lx >= 0 && lx < CHUNK_SIZE &&
               ly > 0 && ly < CHUNK_HEIGHT &&
               lz >= 0 && lz < CHUNK_SIZE;
    }

    void setIfAirOrLeaves(Chunk& chunk,
                          int lx,
                          int ly,
                          int lz,
                          BlockId id) const {
        if (!inChunk(lx, ly, lz)) return;

        BlockId existing = chunk.get(lx, ly, lz);

        if (existing == AIR || existing == LEAVES) {
            chunk.set(lx, ly, lz, id);
        }
    }

    void setLeavesIfAir(Chunk& chunk,
                        int lx,
                        int ly,
                        int lz) const {
        if (!inChunk(lx, ly, lz)) return;

        if (chunk.get(lx, ly, lz) == AIR) {
            chunk.set(lx, ly, lz, LEAVES);
        }
    }

    void placeTree(Chunk& chunk,
                   int lx,
                   int ly,
                   int lz,
                   int worldX,
                   int worldZ,
                   Biome biome) const {
        uint32_t th = hash3(worldX, 5, worldZ, seed + 620);

        int trunkH = 4 + static_cast<int>(th % 3);

        if (biome == Biome::Forest && ((th >> 8) & 7u) == 0u) {
            trunkH += 1;
        }

        for (int dy = 0; dy < trunkH; ++dy) {
            int py = ly + dy;
            setIfAirOrLeaves(chunk, lx, py, lz, WOOD);
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
                        uint32_t cornerHash = hash3(worldX + dx, py, worldZ + dz, seed + 621);

                        if ((cornerHash & 3u) != 0u) {
                            continue;
                        }
                    }

                    if (dy == 3 && ax + az > 1) {
                        continue;
                    }

                    if (dx == 0 && dz == 0 && py < ly + trunkH) {
                        continue;
                    }

                    setLeavesIfAir(chunk, lx + dx, py, lz + dz);
                }
            }
        }

        setIfAirOrLeaves(chunk, lx, ly + trunkH - 1, lz, WOOD);
    }

    void placeTreesForChunk(Chunk& chunk,
                            HeightCache& cache,
                            int baseX,
                            int baseZ) const {
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
                int tx = 0;
                int tz = 0;
                int ty = 0;
                Biome biome = Biome::Plains;

                if (!treeCandidate(cx, cz, cache, tx, tz, ty, biome)) {
                    continue;
                }

                placeTree(
                    chunk,
                    tx - baseX,
                    ty,
                    tz - baseZ,
                    tx,
                    tz,
                    biome
                );
            }
        }
    }

    uint32_t seed;
};

} // namespace mc
