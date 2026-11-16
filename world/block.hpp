#pragma once
#include "../core/types.hpp"

namespace mc {

enum class BlockType : BlockId {
    Air         = 0,
    Grass       = 1,
    Dirt        = 2,
    Stone       = 3,
    Sand        = 4,
    Wood        = 5,
    Leaves      = 6,
    Water       = 7,
    Bedrock     = 8,
    Snow        = 9,
    Sandstone   = 10,
    Gravel      = 11,
    CoalOre     = 12,   // NEW
    IronOre     = 13,   // NEW
    GoldOre     = 14,   // NEW
    RedstoneOre = 15,   // NEW
    DiamondOre  = 16,   // NEW
    Clay        = 17,   // NEW
};

struct BlockDef {
    bool solid;
    bool transparent;
    uint8_t texTop, texSide, texBottom;
};

constexpr BlockDef BLOCK_DEFS[] = {
    /* Air         */ {false, true,  0,  0,  0},
    /* Grass       */ {true,  false, 0,  1,  2},
    /* Dirt        */ {true,  false, 2,  2,  2},
    /* Stone       */ {true,  false, 3,  3,  3},
    /* Sand        */ {true,  false, 4,  4,  4},
    /* Wood        */ {true,  false, 6,  5,  6},
    /* Leaves      */ {true,  true,  7,  7,  7},
    /* Water       */ {false, true,  8,  8,  8},
    /* Bedrock     */ {true,  false, 9,  9,  9},
    /* Snow        */ {true,  false, 10, 10, 10},
    /* Sandstone   */ {true,  false, 11, 11, 11},
    /* Gravel      */ {true,  false, 12, 12, 12},
    /* CoalOre     */ {true,  false, 13, 13, 13},
    /* IronOre     */ {true,  false, 14, 14, 14},
    /* GoldOre     */ {true,  false, 15, 15, 15},
    /* RedstoneOre */ {true,  false, 16, 16, 16},
    /* DiamondOre  */ {true,  false, 17, 17, 17},
    /* Clay        */ {true,  false, 18, 18, 18},
};

constexpr size_t BLOCK_DEF_COUNT = sizeof(BLOCK_DEFS) / sizeof(BLOCK_DEFS[0]);

inline const BlockDef& blockDef(BlockId id) {
    if (id >= BLOCK_DEF_COUNT) id = 0;
    return BLOCK_DEFS[id];
}

inline bool isSolid(BlockId id)       { return blockDef(id).solid; }
inline bool isTransparent(BlockId id) { return blockDef(id).transparent; }

} // namespace mc
