#include "mesh_builder.hpp"
#include "../world/block.hpp"
#include "../core/math.hpp"

namespace mc {

namespace {

struct Face {
    Vec3 normal;
    int  v[4][3];
    bool top, bottom, side;
};

const Face FACES[6] = {
    // +X (right)
    {{ 1, 0, 0}, {{1,0,1},{1,0,0},{1,1,0},{1,1,1}}, false,false,true},
    // -X (left)
    {{-1, 0, 0}, {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}, false,false,true},
    // +Y (top)
    {{ 0, 1, 0}, {{0,1,1},{1,1,1},{1,1,0},{0,1,0}}, true,false,false},
    // -Y (bottom)
    {{ 0,-1, 0}, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}}, false,true,false},
    // +Z (front)
    {{ 0, 0, 1}, {{0,0,1},{1,0,1},{1,1,1},{0,1,1}}, false,false,true},
    // -Z (back)
    {{ 0, 0,-1}, {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}, false,false,true},
};

void addFace(std::vector<Vertex>& out, const Face& f,
             int bx, int by, int bz, int tileIndex) {
    constexpr float TILES_PER_ROW = 16.0f;
    int tx = tileIndex % 16;
    int ty = tileIndex / 16;
    float u0 = tx / TILES_PER_ROW;
    float v0 = ty / TILES_PER_ROW;
    float du = 1.0f / TILES_PER_ROW;
    float dv = 1.0f / TILES_PER_ROW;

    constexpr float UV[4][2] = {{0,0},{1,0},{1,1},{0,1}};

    Vertex verts[4];
    for (int i = 0; i < 4; ++i) {
        verts[i].x  = static_cast<float>(bx + f.v[i][0]);
        verts[i].y  = static_cast<float>(by + f.v[i][1]);
        verts[i].z  = static_cast<float>(bz + f.v[i][2]);
        verts[i].u  = u0 + UV[i][0] * du;
        verts[i].v  = v0 + UV[i][1] * dv;
        verts[i].nx = f.normal.x;
        verts[i].ny = f.normal.y;
        verts[i].nz = f.normal.z;
    }
    out.push_back(verts[0]); out.push_back(verts[1]); out.push_back(verts[2]);
    out.push_back(verts[0]); out.push_back(verts[2]); out.push_back(verts[3]);
}

inline BlockId neighborBlock(const Chunk& center,
                             const Chunk* north, const Chunk* south,
                             const Chunk* west,  const Chunk* east,
                             int x, int y, int z) {
    if (y < 0 || y >= CHUNK_HEIGHT) return 0;

    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE) {
        return center.get(x, y, z);
    }

    const Chunk* n = nullptr;
    int nx = x, nz = z;

    if (z < 0)                { n = north; nz = z + CHUNK_SIZE; }
    else if (z >= CHUNK_SIZE) { n = south; nz = z - CHUNK_SIZE; }
    else if (x < 0)           { n = west;  nx = x + CHUNK_SIZE; }
    else if (x >= CHUNK_SIZE) { n = east;  nx = x - CHUNK_SIZE; }

    if (!n) return 8;  // Bedrock — считаем твёрдым
    return n->get(nx, y, nz);
}

} // namespace

void buildChunkGeometry(
    const Chunk& center,
    const Chunk* north, const Chunk* south,
    const Chunk* west,  const Chunk* east,
    std::vector<Vertex>& out)
{
    out.clear();
    out.reserve(4096 * 6);

    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                BlockId id = center.get(x, y, z);
                if (id == 0) continue;

                const BlockDef& def = blockDef(id);

                for (int fi = 0; fi < 6; ++fi) {
                    const Face& f = FACES[fi];
                    int nbx = x + static_cast<int>(f.normal.x);
                    int nby = y + static_cast<int>(f.normal.y);
                    int nbz = z + static_cast<int>(f.normal.z);

                    BlockId neighbor = neighborBlock(center, north, south, west, east,
                                                     nbx, nby, nbz);

                    if (isSolid(neighbor) && !isTransparent(neighbor)) continue;
                    if (id == neighbor && id == 7) continue;  // Water-Water

                    int tile = f.top    ? def.texTop
                             : f.bottom ? def.texBottom
                                        : def.texSide;
                    addFace(out, f, x, y, z, tile);
                }
            }
        }
    }
}

} // namespace mc
