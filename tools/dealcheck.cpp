// Cross-platform determinism check: print a fingerprint of the deal for each seed.
// The winnable-pool scheme requires that a seed reproduce the identical deal in the
// solver, native game, and wasm game. Build native and via em++ and diff the output.
//
//   c++  -std=c++20 tools/dealcheck.cpp src/game.cpp -I src -o /tmp/dc_native
//   em++ -std=c++20 tools/dealcheck.cpp src/game.cpp -I src -o /tmp/dc.js && node /tmp/dc.js
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "game.hpp"

int main(int argc, char** argv) {
    int n = argc > 1 ? std::atoi(argv[1]) : 64;
    for (int seed = 0; seed < n; ++seed) {
        Game g;
        g.rng.seed((uint64_t)seed);
        g.deal();
        uint64_t h = 1469598103934665603ULL;  // FNV-1a over the dealt layout
        auto mix = [&](int v) {
            h ^= (uint64_t)(v + 1);
            h *= 1099511628211ULL;
        };
        for (int c = 0; c < 7; ++c) {
            for (const auto& card : g.tableau[c]) mix((int)card.suit * 13 + card.rank - 1);
            mix(99);  // column separator
        }
        for (const auto& card : g.stock) mix((int)card.suit * 13 + card.rank - 1);
        std::printf("%d %016llx\n", seed, (unsigned long long)h);
    }
    return 0;
}
