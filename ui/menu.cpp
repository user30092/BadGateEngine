#include "menu.hpp"
#include "../core/math.hpp" // clampf
#include <cstdio>
#include <cstring>

namespace mc {

    void Settings::save(const std::string& path) const {
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return;
        std::fprintf(f, "renderDistance=%d\n", renderDistance);
        std::fprintf(f, "lookSensitivity=%.4f\n", lookSensitivity);
        std::fprintf(f, "musicEnabled=%d\n", musicEnabled ? 1 : 0);
        std::fprintf(f, "lastHost=%s\n", lastHost.c_str());
        std::fprintf(f, "lastPort=%u\n", static_cast<unsigned>(lastPort));
        if (!playerName.empty())
            std::fprintf(f, "playerName=%s\n", playerName.c_str());
        std::fclose(f);
    }

    Settings Settings::load(const std::string& path) {
        Settings s;
        FILE* f = std::fopen(path.c_str(), "r");
        if (!f) return s;

        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            int ival; float fval; char sval[224];
            if (std::sscanf(line, "renderDistance=%d", &ival) == 1) {
                s.renderDistance = ival >= 2 ? (ival <= 32 ? ival : 32) : 2;
            } else if (std::sscanf(line, "lookSensitivity=%f", &fval) == 1) {
                s.lookSensitivity = clampf(fval, 0.02f, 1.0f);
            } else if (std::sscanf(line, "musicEnabled=%d", &ival) == 1) {
                s.musicEnabled = (ival != 0);
            } else if (std::sscanf(line, "lastHost=%223s", sval) == 1) {
                s.lastHost = sval;
            } else if (std::sscanf(line, "lastPort=%d", &ival) == 1) {
                s.lastPort = static_cast<uint16_t>(ival);
            } else if (std::sscanf(line, "playerName=%223s", sval) == 1) {
                s.playerName = sval;
            }
        }
        std::fclose(f);
        return s;
    }


} // namespace mc
