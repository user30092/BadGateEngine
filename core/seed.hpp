#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <cctype>
#include <cstdio>
#include <charconv>          // std::from_chars (C++17, без исключений)

namespace mc {

inline uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// localtime_r — POSIX.  На Windows есть только localtime_s с обратным
// порядком аргументов.  Оборачиваем, чтобы не плодить #ifdef в вызывающем
// коде и не ломать сборку MSVC.
inline bool localtime_safe(std::time_t t, std::tm& out) {
#ifdef _WIN32
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}

// Сид = hash(YYYY-MM-DD + "|" + соль). Соль пустая -> сид зависит только
// от даты создания; все миры, созданные в один день без своей соли,
// получат одинаковый сид (это осознанное поведение, не баг — соль как раз
// и существует, чтобы при желании получить другой мир в тот же день).
inline uint32_t seedFromDateAndSalt(const std::string& salt) {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    if (!localtime_safe(t, tmv)) return fnv1a(salt);

    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return fnv1a(std::string(buf) + "|" + salt);
}

// Если игрок вручную ввёл сид текстом в UI. Чисто числовая строка
// используется как есть (привычное поведение из ванильного Minecraft),
// иначе хэшируется как произвольный текст.
//
// Используем std::from_chars вместо std::stoul + try/catch:
//   - from_chars не бросает исключений (совместим с -fno-exceptions),
//   - при переполнении возвращает std::errc::result_out_of_range,
//     и мы спокойно падаем на хэш.
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
        // переполнение или невалидный ввод — уходим на хэш ниже
    }
    return fnv1a(s);
}

} // namespace mc
