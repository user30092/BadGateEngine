#pragma once
#include <cstdint>

namespace mc {

// xorshift64* — быстрый и качественный PRNG
class Random {
public:
    explicit Random(uint64_t seed = 0x1234567890ABCDEFULL) : state(seed ? seed : 1) {}

    uint64_t next() {
        uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 0x2545F4914F6CDD1DULL;
    }

    float nextFloat() {
        return (next() >> 40) * (1.0f / 16777216.0f);
    }

    int nextInt(int min, int max) {
        if (max <= min) return min;
        return min + static_cast<int>(next() % static_cast<uint64_t>(max - min));
    }

    float range(float lo, float hi) { return lo + nextFloat() * (hi - lo); }

private:
    uint64_t state;
};

inline uint32_t hash3(int x, int y, int z, uint32_t seed = 0) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x8DA6B343u;
    h ^= static_cast<uint32_t>(y) * 0xD8163841u;
    h ^= static_cast<uint32_t>(z) * 0xCB1AB31Fu;
    h ^= h >> 13; h *= 0x85EBCA6Bu;
    h ^= h >> 16;
    return h;
}

inline float noise3(int x, int y, int z, uint32_t seed = 0) {
    return (hash3(x, y, z, seed) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

} // namespace mc
