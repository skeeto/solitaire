// Tiny persistence for the lifetime wins counter and the mute preference.
// Native uses SDL_GetPrefPath (AppData / ~/Library / XDG); web uses localStorage.
#pragma once

struct Stats {
    int wins = 0;
    bool muted = false;
};

Stats loadStats();
void saveStats(const Stats& s);
