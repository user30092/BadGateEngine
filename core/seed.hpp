#pragma once
#include <cstdint>
#include <ctime>
#include <chrono>
#include <string>
#include <cctype>
#include <cstdio>
#include <charconv>

namespace mc {

inline uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// ХАХАхахааЪВЪАХАЪАХЪАЪАХА, я сырник
// Legacy-лод, лодочка делает биб-биб
inline bool localtime_safe(std::time_t t, std::tm& out) {
#ifdef _WIN32
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}


inline uint32_t seedFromTimeAndSalt(const std::string& salt) {
    using namespace std::chrono;
    auto now    = system_clock::now();
    auto millis = duration_cast<milliseconds>(now.time_since_epoch()).count();

    if (millis == 0) return fnv1a(salt);

    return fnv1a(std::to_string(millis) + "|" + salt);
}

//Обратная говносовместимость, удалю когда захочу
inline uint32_t seedFromDateAndSalt(const std::string& salt) {
    return seedFromTimeAndSalt(salt);
}

// Кусок кода который не используется.
// Просверлите мне могзи
inline uint32_t seedFromUserString(const std::string& s) {
    if (s.empty()) return seedFromDateAndSalt("");

    bool allDigits = true;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) { allDigits = false; break; }
    }

    if (allDigits) {
        uint32_t v = 0;
        const char* first = s.data();
        const char* last  = s.data() + s.size();
        auto [ptr, ec] = std::from_chars(first, last, v);
        if (ec == std::errc{} && ptr == last) return v;
    }
    return fnv1a(s);
}

} // namespace mc
