// Portable, deterministic PRNG used for dealing.
//
// We use xoshiro256** (Blackman & Vigna, 2018) with a 256-bit state, so that the
// generator's period and state space are large enough that essentially any of the
// 52! possible shuffles is reachable. Small seeds (including the winnable-deal
// pool, which is keyed by a 32-bit seed) are expanded into the full 256-bit state
// with splitmix64, the author-recommended seeding routine. On-demand generation
// can instead fill all 256 bits directly from a random device via seedState().
//
// Every operation is plain uint64_t integer arithmetic, so a given seed produces
// an identical deal on any compiler/platform (native and Emscripten/wasm alike) --
// which is what lets a shipped pool of "winnable seeds" reproduce the very deals
// the offline solver proved winnable.
#pragma once

#include <cstdint>

// splitmix64 -- expands a single 64-bit seed into a well-distributed stream; used
// to fill xoshiro256**'s state so even seed 0 yields a nonzero, mixed state.
struct SplitMix64 {
    uint64_t x;
    explicit SplitMix64(uint64_t seed) : x(seed) {}
    uint64_t next() {
        uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
};

struct Xoshiro256ss {
    uint64_t s[4] = {0, 0, 0, 0};

    // Expand a small/pool seed into the full 256-bit state (splitmix64).
    void seed(uint64_t value) {
        SplitMix64 sm(value);
        s[0] = sm.next();
        s[1] = sm.next();
        s[2] = sm.next();
        s[3] = sm.next();
    }

    // Seed all 256 bits directly (e.g. assembled from a random device) so that any
    // generator state -- and thus, roughly, any shuffle -- is reachable.
    void seedState(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
        s[0] = a;
        s[1] = b;
        s[2] = c;
        s[3] = d;
    }

    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t next() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    // Unbiased integer in [0, n) by rejection (portable; no floating point). n > 0.
    uint64_t bounded(uint64_t n) {
        const uint64_t threshold = (-n) % n;  // == 2^64 mod n
        for (;;) {
            uint64_t r = next();
            if (r >= threshold) return r % n;
        }
    }
};
