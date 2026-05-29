#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// White noise in [-1, 1].
struct Noise {
    std::mt19937 rng{12345};
    std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    float operator()() { return dist(rng); }
};

// One-pole low-pass; `a` in (0,1], smaller = darker.
struct LowPass {
    float y = 0.0f;
    float a;
    explicit LowPass(float alpha) : a(alpha) {}
    float operator()(float x) {
        y += a * (x - y);
        return y;
    }
};

// A short high-pitched "tick": a fast-decaying sine blip plus an attack click.
std::vector<float> makeClick(int rate, float freq, float seconds, float decay) {
    int n = static_cast<int>(rate * seconds);
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / rate;
        float env = std::exp(-t * decay);
        float body = std::sin(2.0f * kPi * freq * t);
        // Sharp transient in the first couple milliseconds for the "click".
        float click = (i < rate / 500) ? (1.0f - static_cast<float>(i) / (rate / 500.0f)) : 0.0f;
        out[i] = (0.7f * body * env + 0.5f * click) * 0.6f;
    }
    return out;
}

// A paper "flip": filtered noise burst with a quick downward emphasis.
std::vector<float> makeFlip(int rate) {
    int n = static_cast<int>(rate * 0.14f);
    std::vector<float> out(n);
    Noise noise;
    LowPass lp(0.25f);
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / rate;
        // Two-lobe envelope so it reads as a quick riffle rather than a pop.
        float env = std::exp(-t * 26.0f) * (0.6f + 0.4f * std::sin(2.0f * kPi * 38.0f * t));
        out[i] = lp(noise()) * env * 0.5f;
    }
    return out;
}

// An auto-move "swoosh": band-limited noise under a bell-shaped envelope with a
// rising then falling brightness.
std::vector<float> makeSwoosh(int rate) {
    int n = static_cast<int>(rate * 0.26f);
    std::vector<float> out(n);
    Noise noise;
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / rate;
        float p = static_cast<float>(i) / n;  // 0..1
        float env = std::sin(kPi * p);         // bell
        // Sweep the low-pass cutoff up then back down for the "whoosh" motion.
        float alpha = 0.04f + 0.30f * std::sin(kPi * p);
        static thread_local LowPass lp(0.1f);
        lp.a = std::clamp(alpha, 0.02f, 0.5f);
        out[i] = lp(noise()) * env * 0.45f;
    }
    return out;
}

}  // namespace

bool Audio::init() {
    clips_[static_cast<int>(Sfx::Flip)] = makeFlip(kRate);
    clips_[static_cast<int>(Sfx::Pickup)] = makeClick(kRate, 920.0f, 0.045f, 90.0f);
    clips_[static_cast<int>(Sfx::Drop)] = makeClick(kRate, 480.0f, 0.075f, 60.0f);
    clips_[static_cast<int>(Sfx::Swoosh)] = makeSwoosh(kRate);

    SDL_AudioSpec spec{};
    spec.freq = kRate;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        SDL_Log("audio: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Audio::shutdown() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

void Audio::play(Sfx s) {
    if (muted_ || !stream_) return;
    voices_.push_back(Voice{&clips_[static_cast<int>(s)], 0});
}

void Audio::update() {
    if (!stream_) return;

    // Keep roughly 60 ms of audio queued without overshooting.
    const int target = static_cast<int>(kRate * 0.06f);
    int queuedFrames = SDL_GetAudioStreamQueued(stream_) / static_cast<int>(2 * sizeof(float));
    int need = target - queuedFrames;
    if (need <= 0) return;
    need = std::min(need, kRate / 10);  // cap a single top-up at 100 ms

    std::vector<float> out(static_cast<size_t>(need) * 2, 0.0f);
    if (!muted_) {
        for (auto& v : voices_) {
            int avail = static_cast<int>(v.buf->size() - v.pos);
            int frames = std::min(need, avail);
            for (int i = 0; i < frames; ++i) {
                float sample = (*v.buf)[v.pos + i];
                out[i * 2 + 0] += sample;
                out[i * 2 + 1] += sample;
            }
            v.pos += frames;
        }
        // Soft clip so overlapping effects never crack the output.
        for (float& x : out) x = std::tanh(x);
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.pos >= v.buf->size(); }),
                  voices_.end());

    SDL_PutAudioStreamData(stream_, out.data(), static_cast<int>(out.size() * sizeof(float)));
}
