#include "storage.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include <cstdlib>

// localStorage round-trips through small JS shims. Values are plain strings.
EM_JS(int, js_get_int, (const char* key, int fallback), {
    var v = localStorage.getItem(UTF8ToString(key));
    if (v === null) return fallback;
    var n = parseInt(v, 10);
    return isNaN(n) ? fallback : n;
});

EM_JS(void, js_set_int, (const char* key, int value), {
    try { localStorage.setItem(UTF8ToString(key), "" + value); } catch (e) {}
});

Stats loadStats() {
    Stats s;
    s.wins = js_get_int("sawayama_wins", 0);
    s.muted = js_get_int("sawayama_muted", 0) != 0;
    s.tutorialSeen = js_get_int("sawayama_tutorial_seen", 0) != 0;
    return s;
}

void saveStats(const Stats& s) {
    js_set_int("sawayama_wins", s.wins);
    js_set_int("sawayama_muted", s.muted ? 1 : 0);
    js_set_int("sawayama_tutorial_seen", s.tutorialSeen ? 1 : 0);
}

EM_JS(char*, js_get_str, (const char* key), {
    var v = localStorage.getItem(UTF8ToString(key));
    if (v === null) return 0;
    var len = lengthBytesUTF8(v) + 1;
    var p = _malloc(len);
    stringToUTF8(v, p, len);
    return p;
});

EM_JS(void, js_set_str, (const char* key, const char* val), {
    try { localStorage.setItem(UTF8ToString(key), UTF8ToString(val)); } catch (e) {}
});

std::string loadGame() {
    char* p = js_get_str("sawayama_game");
    std::string s = p ? p : "";
    if (p) std::free(p);
    return s;
}

void saveGame(const std::string& blob) { js_set_str("sawayama_game", blob.c_str()); }
void clearGame() { js_set_str("sawayama_game", ""); }

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
        int wins = 0, muted = 0, seen = 0;
        // >= 1 so a two-field file from an older build still loads (seen stays 0).
        if (std::fscanf(f, "%d %d %d", &wins, &muted, &seen) >= 1) {
            s.wins = wins;
            s.muted = muted != 0;
            s.tutorialSeen = seen != 0;
        }
        std::fclose(f);
    }
    return s;
}

void saveStats(const Stats& s) {
    std::string path = statsPath();
    if (path.empty()) return;
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fprintf(f, "%d %d %d\n", s.wins, s.muted ? 1 : 0, s.tutorialSeen ? 1 : 0);
        std::fclose(f);
    }
}

static std::string gamePath() {
    char* base = SDL_GetPrefPath("wellons", "SawayamaSolitaire");
    if (!base) return {};
    std::string p = std::string(base) + "game.txt";
    SDL_free(base);
    return p;
}

std::string loadGame() {
    std::string path = gamePath();
    if (path.empty()) return {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

void saveGame(const std::string& blob) {
    std::string path = gamePath();
    if (path.empty()) return;
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(blob.data(), 1, blob.size(), f);
        std::fclose(f);
    }
}

void clearGame() { saveGame(std::string()); }
#endif
