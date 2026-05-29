#include "storage.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// localStorage round-trips through small JS shims. Values are plain strings.
EM_JS(int, js_get_int, (const char* key, int fallback), {
    var v = localStorage.getItem(UTF8ToString(key));
    if (v === null) return fallback;
    var n = parseInt(v, 10);
    return isNaN(n) ? fallback : n;
});

EM_JS(void, js_set_int, (const char* key, int value), {
    try { localStorage.setItem(UTF8ToString(key), '' + value); } catch (e) {}
});

Stats loadStats() {
    Stats s;
    s.wins = js_get_int("sawayama_wins", 0);
    s.muted = js_get_int("sawayama_muted", 0) != 0;
    return s;
}

void saveStats(const Stats& s) {
    js_set_int("sawayama_wins", s.wins);
    js_set_int("sawayama_muted", s.muted ? 1 : 0);
}

#else  // native
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

static std::string statsPath() {
    char* base = SDL_GetPrefPath("wellons", "SawayamaSolitaire");
    if (!base) return {};
    std::string p = std::string(base) + "stats.txt";
    SDL_free(base);
    return p;
}

Stats loadStats() {
    Stats s;
    std::string path = statsPath();
    if (path.empty()) return s;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        int wins = 0, muted = 0;
        if (std::fscanf(f, "%d %d", &wins, &muted) >= 1) {
            s.wins = wins;
            s.muted = muted != 0;
        }
        std::fclose(f);
    }
    return s;
}

void saveStats(const Stats& s) {
    std::string path = statsPath();
    if (path.empty()) return;
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fprintf(f, "%d %d\n", s.wins, s.muted ? 1 : 0);
        std::fclose(f);
    }
}
#endif
