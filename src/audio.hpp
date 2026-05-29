// Asset-free sound effects: short PCM buffers synthesized at startup and mixed
// into a single SDL audio stream. All effects are mono, summed onto both
// channels at output time.
#pragma once

#include <SDL3/SDL.h>

#include <vector>

enum class Sfx { Flip, Pickup, Drop, Swoosh };

class Audio {
public:
    bool init();
    void shutdown();

    void play(Sfx s);
    // Top up the output stream; call once per frame.
    void update();

    void setMuted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }

private:
    static constexpr int kRate = 44100;

    struct Voice {
        const std::vector<float>* buf = nullptr;
        size_t pos = 0;
    };

    std::vector<float> clips_[4];          // one per Sfx
    std::vector<Voice> voices_;
    SDL_AudioStream* stream_ = nullptr;
    bool muted_ = false;
};
