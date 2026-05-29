// Tiny persistence for the lifetime wins counter, the mute preference, and the
// in-progress game. Native uses SDL_GetPrefPath (AppData / ~/Library / XDG);
// web uses localStorage.
#pragma once

#include <string>

struct Stats {
    int wins = 0;
    bool muted = false;
};

Stats loadStats();
void saveStats(const Stats& s);

// Serialized in-progress game (opaque blob). loadGame returns "" if none saved.
std::string loadGame();
void saveGame(const std::string& blob);
void clearGame();
