#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <type_traits>
#include <limits>

namespace mc {

enum class PacketType : uint8_t {
    Handshake   = 0x01,
    KeepAlive   = 0x02,
    ChunkData   = 0x10,
    BlockUpdate = 0x11,
    PlayerMove  = 0x20,
    ChatMessage = 0x30,
    PlayerLeave = 0x21,
};

// ==========================================================================
// Wire protocol contract (Android ARM/ARM64 + PC x86/x86_64):
//   - Все целые и float на проводе — little-endian.
//   - float — IEEE 754 single precision, 4 байта.
//   - Заголовок — ровно 9 байт, без padding.
// static_assert'ы ниже ловят нарушения на этапе компиляции.
// ==========================================================================

static_assert(sizeof(float)     == 4, "Protocol assumes 32-bit IEEE 754 float");
static_assert(std::numeric_limits<float>::is_iec559, "Protocol assumes IEEE 754 float");
static_assert(sizeof(uint32_t)  == 4, "Protocol assumes 32-bit uint32_t");
static_assert(sizeof(int32_t)   == 4, "Protocol assumes 32-bit int32_t");

// Заголовок — ровно 9 байт. Без pack было бы 12 (padding после type).
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  type;
    uint32_t size;
    uint32_t seq;
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 9, "PacketHeader must be 9 bytes on the wire");

// ==========================================================================
// LE helpers
//
// На текущих целевых платформах (ARM и x86) — обе LE, поэтому это no-op.
// Если появится BE-платформа, сработает ветка с bswap. Windows-ветка нужна
// потому что MSVC не определяет __BYTE_ORDER__.
// ==========================================================================
#if defined(_WIN32)
    inline uint32_t toLE32(uint32_t v)   { return v; }
    inline uint32_t fromLE32(uint32_t v) { return v; }
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    inline uint32_t toLE32(uint32_t v)   { return v; }
    inline uint32_t fromLE32(uint32_t v) { return v; }
#else
    inline uint32_t toLE32(uint32_t v)   { return __builtin_bswap32(v); }
    inline uint32_t fromLE32(uint32_t v) { return __builtin_bswap32(v); }
#endif

class PacketWriter {
public:
    void writeByte(uint8_t v) { buf.push_back(v); }

    void writeVarInt(uint32_t v) {
        while (v >= 0x80) {
            buf.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(v));
    }

    void writeBytes(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), p, p + n);
    }

    // Явное LE-кодирование: те же байты, что memcpy на LE-хосте, но
    // кроссплатформенно корректно на BE.
    void writeFloat(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        writeU32(bits);
    }
    void writeI32(int32_t v) { writeU32(static_cast<uint32_t>(v)); }
    void writeU32(uint32_t v) {
        uint32_t le = toLE32(v);
        buf.push_back(static_cast<uint8_t>(le));
        buf.push_back(static_cast<uint8_t>(le >> 8));
        buf.push_back(static_cast<uint8_t>(le >> 16));
        buf.push_back(static_cast<uint8_t>(le >> 24));
    }

    const std::vector<uint8_t>& data() const { return buf; }

private:
    std::vector<uint8_t> buf;
};

class PacketReader {
public:
    PacketReader(const uint8_t* d, size_t s) : data_(d), size_(s) {}

    uint8_t readByte() { return pos < size_ ? data_[pos++] : 0; }

    uint32_t readVarInt() {
        uint32_t v = 0; int shift = 0;
        while (pos < size_) {
            uint8_t b = data_[pos++];
            v |= static_cast<uint32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
            if (shift >= 35) break;   // защита от битого varint
        }
        return v;
    }

    float readFloat() {
        uint32_t bits = readU32();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    int32_t readI32() { return static_cast<int32_t>(readU32()); }
    uint32_t readU32() {
        if (pos + 4 > size_) return 0;
        uint32_t le =
            static_cast<uint32_t>(data_[pos])
          | (static_cast<uint32_t>(data_[pos + 1]) << 8)
          | (static_cast<uint32_t>(data_[pos + 2]) << 16)
          | (static_cast<uint32_t>(data_[pos + 3]) << 24);
        pos += 4;
        return fromLE32(le);
    }

    bool eof() const { return pos >= size_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos = 0;
};

constexpr uint32_t MAX_PACKET_SIZE = 4 * 1024 * 1024;

class PacketAssembler {
public:
    void feed(const uint8_t* data, size_t n) {
        buf.insert(buf.end(), data, data + n);
    }

    bool tryPop(PacketType& outType, std::vector<uint8_t>& outPayload) {
        if (buf.size() < sizeof(PacketHeader)) return false;

        PacketHeader h;
        std::memcpy(&h, buf.data(), sizeof(PacketHeader));

        if (h.size > MAX_PACKET_SIZE) {
            desynced = true;
            return false;
        }

        size_t total = sizeof(PacketHeader) + h.size;
        if (buf.size() < total) return false;

        outType = static_cast<PacketType>(h.type);
        outPayload.assign(buf.begin() + sizeof(PacketHeader), buf.begin() + total);
        buf.erase(buf.begin(), buf.begin() + total);
        return true;
    }

    bool isDesynced() const { return desynced; }

private:
    std::vector<uint8_t> buf;
    bool desynced = false;
};

} // namespace mc
