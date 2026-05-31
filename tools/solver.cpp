// Sawayama Solitaire winnability solver + Monte Carlo analysis.
//
// The game is perfect-information and deterministic given the deal, so this is a
// graph search for a path that empties everything onto the foundations. The
// conservative auto-mover doesn't affect winnability (it never blocks a win, and
// double-click can force any foundation-ready card), so the solver treats every
// foundation-ready exposed card as movable. It reuses the game's Game model only
// to generate deals (matching the real ace-filtered shuffle).
//
// Search: iterative DFS over canonicalized states with a transposition table.
//   - "Safe" foundation moves (the conservative auto-mover rule, which is
//     provably never-needed) are auto-applied -- a sound, completeness-preserving
//     reduction. Unsafe foundation moves are kept as optional branches.
//   - The 7 tableau columns are interchangeable, so they're sorted before
//     hashing; empty columns are interchangeable, so only the first is a target.
//   - Per-deal node budget => each deal returns solved / unsolvable / undetermined
//     (budget hit). The win rate is therefore a measured lower bound.
//
// Usage:
//   solver [--deals N] [--start S] [--budget B] [--threads T]
//   solver --seed S                 # solve one deal, print the outcome
//
// Seeds are reproducible with the same build (std::mt19937_64 + std::shuffle).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "game.hpp"

namespace {

// Card encoding matches Game::serialize: id = suit*13 + (rank-1), suits in
// {Hearts=0, Diamonds=1, Spades=2, Clubs=3}; Hearts/Diamonds are red.
inline int csuit(uint8_t c) { return c / 13; }
inline int crank(uint8_t c) { return c % 13 + 1; }
inline bool cred(uint8_t c) { int s = c / 13; return s == 0 || s == 1; }
inline bool canStackOn(uint8_t moving, uint8_t onto) {
    return crank(moving) == crank(onto) - 1 && cred(moving) != cred(onto);
}

struct State {
    std::vector<uint8_t> col[7];  // bottom..top; top (back) is the exposed card
    std::vector<uint8_t> stock;   // drawn from the back
    std::vector<uint8_t> waste;   // top (back) is the only playable card
    uint8_t found[4] = {0, 0, 0, 0};
    int cell = -1;  // free cell card (-1 = empty); usable only once stock is gone
};

// The free cell unlocks permanently once the stock is exhausted; since the stock
// only shrinks, "stock empty" is equivalent to "free cell available".
inline bool cellActive(const State& s) { return s.stock.empty(); }

inline bool isWin(const State& s) {
    return s.found[0] == 13 && s.found[1] == 13 && s.found[2] == 13 && s.found[3] == 13;
}

inline bool foundationReady(const State& s, uint8_t c) {
    return s.found[csuit(c)] == crank(c) - 1;
}

// Smallest opposite-color rank still in play (not on a foundation), or INT_MAX.
inline int minOpp(const State& s, int suit) {
    bool red = (suit == 0 || suit == 1);
    int best = INT_MAX;
    for (int i = 0; i < 4; ++i) {
        if ((i == 0 || i == 1) == red) continue;
        if (s.found[i] >= 13) continue;
        best = std::min(best, s.found[i] + 1);
    }
    return best;
}

// The conservative auto-mover rule (kept in sync with Game::autoEligible). These
// cards are provably never needed in the tableau, so auto-applying them is sound.
inline bool autoEligible(const State& s, uint8_t c) {
    if (!foundationReady(s, c)) return false;
    if (crank(c) <= 2) return true;
    int m = minOpp(s, csuit(c));
    if (m == INT_MAX) return true;
    return crank(c) <= m;
}

void autoAdvance(State& s) {
    bool moved = true;
    while (moved) {
        moved = false;
        for (int i = 0; i < 7; ++i)
            if (!s.col[i].empty() && autoEligible(s, s.col[i].back())) {
                uint8_t c = s.col[i].back();
                s.col[i].pop_back();
                s.found[csuit(c)] = crank(c);
                moved = true;
            }
        if (!s.waste.empty() && autoEligible(s, s.waste.back())) {
            uint8_t c = s.waste.back();
            s.waste.pop_back();
            s.found[csuit(c)] = crank(c);
            moved = true;
        }
        if (s.cell >= 0 && autoEligible(s, (uint8_t)s.cell)) {
            uint8_t c = (uint8_t)s.cell;
            s.cell = -1;
            s.found[csuit(c)] = crank(c);
            moved = true;
        }
    }
}

void moveRun(State& ns, int src, int j, int dst) {
    auto& cs = ns.col[src];
    auto& cd = ns.col[dst];
    for (size_t t = j; t < cs.size(); ++t) cd.push_back(cs[t]);
    cs.resize(j);
}

// Generate children (each is one raw move; the caller auto-advances them). Moves
// are ordered foundation -> build -> stash -> draw so the DFS tries progress first.
void genChildren(const State& s, std::vector<State>& out) {
    int firstEmpty = -1;
    for (int d = 0; d < 7; ++d)
        if (s.col[d].empty()) { firstEmpty = d; break; }

    // 1. Unsafe foundation moves (safe ones were already auto-advanced).
    auto sendUp = [&](uint8_t c, int fromCol, bool fromWaste, bool fromCell) {
        if (!foundationReady(s, c) || autoEligible(s, c)) return;
        State ns = s;
        if (fromCol >= 0) ns.col[fromCol].pop_back();
        else if (fromWaste) ns.waste.pop_back();
        else if (fromCell) ns.cell = -1;
        ns.found[csuit(c)] = crank(c);
        out.push_back(std::move(ns));
    };
    for (int i = 0; i < 7; ++i)
        if (!s.col[i].empty()) sendUp(s.col[i].back(), i, false, false);
    if (!s.waste.empty()) sendUp(s.waste.back(), -1, true, false);
    if (s.cell >= 0) sendUp((uint8_t)s.cell, -1, false, true);

    // 2. Tableau run -> tableau (any valid descending/alt-color suffix).
    for (int src = 0; src < 7; ++src) {
        const auto& cs = s.col[src];
        if (cs.empty()) continue;
        int len = (int)cs.size();
        int k = len - 1;
        while (k > 0 && canStackOn(cs[k], cs[k - 1])) --k;  // longest movable run
        for (int j = len - 1; j >= k; --j) {
            uint8_t bottom = cs[j];  // the card that lands on the destination
            for (int dst = 0; dst < 7; ++dst) {
                if (dst == src) continue;
                if (s.col[dst].empty()) {
                    if (dst != firstEmpty || j == 0) continue;  // empties equal; whole-col is a no-op
                } else if (!canStackOn(bottom, s.col[dst].back())) {
                    continue;
                }
                State ns = s;
                moveRun(ns, src, j, dst);
                out.push_back(std::move(ns));
            }
        }
    }

    // 3. Waste top -> tableau.
    if (!s.waste.empty()) {
        uint8_t c = s.waste.back();
        for (int dst = 0; dst < 7; ++dst) {
            if (s.col[dst].empty()) {
                if (dst != firstEmpty) continue;
            } else if (!canStackOn(c, s.col[dst].back())) {
                continue;
            }
            State ns = s;
            ns.waste.pop_back();
            ns.col[dst].push_back(c);
            out.push_back(std::move(ns));
        }
    }

    // 4. Free cell -> tableau.
    if (s.cell >= 0) {
        uint8_t c = (uint8_t)s.cell;
        for (int dst = 0; dst < 7; ++dst) {
            if (s.col[dst].empty()) {
                if (dst != firstEmpty) continue;
            } else if (!canStackOn(c, s.col[dst].back())) {
                continue;
            }
            State ns = s;
            ns.cell = -1;
            ns.col[dst].push_back(c);
            out.push_back(std::move(ns));
        }
    }

    // 5. Stash an exposed card into the free cell.
    if (cellActive(s) && s.cell < 0) {
        for (int src = 0; src < 7; ++src)
            if (!s.col[src].empty()) {
                State ns = s;
                ns.cell = ns.col[src].back();
                ns.col[src].pop_back();
                out.push_back(std::move(ns));
            }
        if (!s.waste.empty()) {
            State ns = s;
            ns.cell = ns.waste.back();
            ns.waste.pop_back();
            out.push_back(std::move(ns));
        }
    }

    // 6. Draw three from the stock (single pass; deterministic).
    if (!s.stock.empty()) {
        State ns = s;
        for (int i = 0; i < 3 && !ns.stock.empty(); ++i) {
            ns.waste.push_back(ns.stock.back());
            ns.stock.pop_back();
        }
        out.push_back(std::move(ns));
    }
}

// Canonical 64-bit hash: columns sorted (interchangeable), with separators.
uint64_t canonHash(const State& s) {
    const std::vector<uint8_t>* cols[7];
    for (int i = 0; i < 7; ++i) cols[i] = &s.col[i];
    std::sort(cols, cols + 7,
              [](const std::vector<uint8_t>* a, const std::vector<uint8_t>* b) { return *a < *b; });
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    for (auto cp : cols) {
        mix(0xFF);
        for (uint8_t b : *cp) mix(b);
    }
    mix(0xFE);
    for (uint8_t b : s.stock) mix(b);
    mix(0xFD);
    for (uint8_t b : s.waste) mix(b);
    mix(0xFC);
    for (int i = 0; i < 4; ++i) mix(s.found[i]);
    mix(0xFB);
    mix((uint8_t)(s.cell + 1));
    return h;
}

// Cheap progress heuristic: foundation cards already up, plus exposed cards that
// are about to auto-advance. The DFS explores higher-scoring children first so
// winnable deals beeline toward the goal.
inline int progressScore(const State& s) {
    int sc = s.found[0] + s.found[1] + s.found[2] + s.found[3];
    for (int i = 0; i < 7; ++i)
        if (!s.col[i].empty() && foundationReady(s, s.col[i].back())) ++sc;
    if (!s.waste.empty() && foundationReady(s, s.waste.back())) ++sc;
    if (s.cell >= 0 && foundationReady(s, (uint8_t)s.cell)) ++sc;
    return sc;
}

enum Outcome { SOLVED, UNSOLVABLE, UNDETERMINED };

struct Frame {
    std::vector<State> kids;
    size_t idx = 0;
};

Outcome solve(const State& init, size_t budget, size_t* nodesOut = nullptr) {
    std::unordered_set<uint64_t> closed;
    closed.reserve(1 << 15);
    std::vector<Frame> stack;
    size_t nodes = 0;
    struct NodesGuard {
        size_t* o;
        size_t& n;
        ~NodesGuard() { if (o) *o = n; }
    } guard{nodesOut, nodes};

    auto expand = [&](State st) -> int {  // 1 = win, 0 = pushed, -1 = already seen
        ++nodes;
        autoAdvance(st);
        if (isWin(st)) return 1;
        if (!closed.insert(canonHash(st)).second) return -1;
        Frame f;
        genChildren(st, f.kids);
        size_t nk = f.kids.size();
        if (nk > 1) {  // explore higher-progress children first (scores precomputed once)
            std::vector<std::pair<int, size_t>> order(nk);
            for (size_t i = 0; i < nk; ++i) order[i] = {progressScore(f.kids[i]), i};
            std::stable_sort(order.begin(), order.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
            std::vector<State> reordered;
            reordered.reserve(nk);
            for (auto& pr : order) reordered.push_back(std::move(f.kids[pr.second]));
            f.kids.swap(reordered);
        }
        stack.push_back(std::move(f));
        return 0;
    };

    if (expand(init) == 1) return SOLVED;
    while (!stack.empty()) {
        if (nodes >= budget) return UNDETERMINED;
        Frame& f = stack.back();
        if (f.idx >= f.kids.size()) {
            stack.pop_back();
            continue;
        }
        State child = std::move(f.kids[f.idx++]);
        if (expand(std::move(child)) == 1) return SOLVED;  // expand may invalidate f; not reused
    }
    return UNSOLVABLE;
}

State dealSeed(uint64_t seed) {
    Game g;
    g.rng.seed(seed);
    g.deal();
    State s;
    for (int i = 0; i < 7; ++i)
        for (const auto& c : g.tableau[i])
            s.col[i].push_back((uint8_t)((int)c.suit * 13 + (c.rank - 1)));
    for (const auto& c : g.stock)
        s.stock.push_back((uint8_t)((int)c.suit * 13 + (c.rank - 1)));
    for (int i = 0; i < 4; ++i) s.found[i] = (uint8_t)g.foundation[i];
    return s;
}

struct Tally {
    long solved = 0, unsolvable = 0, undetermined = 0;
    std::vector<uint64_t> unsolvableSeeds;
    std::vector<uint64_t> undeterminedSeeds;
};

struct Wilson {
    double lo, hi;
};
Wilson wilson(long k, long n, double z = 1.96) {
    if (n == 0) return {0, 0};
    double p = (double)k / n, z2 = z * z, denom = 1 + z2 / n;
    double center = (p + z2 / (2 * n)) / denom;
    double half = (z / denom) * std::sqrt(p * (1 - p) / n + z2 / (4.0 * n * n));
    return {center - half, center + half};
}

long argLong(int argc, char** argv, const char* flag, long def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return std::atol(argv[i + 1]);
    return def;
}

}  // namespace

int main(int argc, char** argv) {
    size_t budget = (size_t)argLong(argc, argv, "--budget", 500000);

    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--seed") == 0) {
            uint64_t seed = (uint64_t)std::atoll(argv[i + 1]);
            State init = dealSeed(seed);
            State adv = init;
            autoAdvance(adv);
            std::vector<State> kids;
            genChildren(adv, kids);
            size_t nodes = 0;
            Outcome o = solve(init, budget, &nodes);
            std::printf("seed %llu: %s  (initial children: %zu, nodes: %zu)\n",
                        (unsigned long long)seed,
                        o == SOLVED ? "solvable" : o == UNSOLVABLE ? "unsolvable" : "undetermined",
                        kids.size(), nodes);
            return 0;
        }

    long deals = argLong(argc, argv, "--deals", 1000);
    uint64_t start = (uint64_t)argLong(argc, argv, "--start", 0);
    int threads = (int)argLong(argc, argv, "--threads", 0);
    if (threads <= 0) threads = std::max(1u, std::thread::hardware_concurrency());
    threads = std::min<long>(threads, deals);

    std::printf("Sawayama Solitaire — Monte Carlo winnability\n");
    std::printf("deals: %ld (seeds %llu..%llu), budget: %zu states/deal, threads: %d\n\n", deals,
                (unsigned long long)start, (unsigned long long)(start + deals - 1), budget, threads);

    std::vector<Tally> partials(threads);
    std::vector<std::thread> pool;
    auto t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < threads; ++t) {
        long lo = start + (long)((__int128)deals * t / threads);
        long hi = start + (long)((__int128)deals * (t + 1) / threads);
        pool.emplace_back([&, t, lo, hi] {
            Tally& tl = partials[t];
            for (long seed = lo; seed < hi; ++seed) {
                Outcome o = solve(dealSeed((uint64_t)seed), budget);
                if (o == SOLVED) tl.solved++;
                else if (o == UNSOLVABLE) { tl.unsolvable++; tl.unsolvableSeeds.push_back(seed); }
                else { tl.undetermined++; tl.undeterminedSeeds.push_back(seed); }
            }
        });
    }
    for (auto& th : pool) th.join();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    Tally all;
    for (auto& p : partials) {
        all.solved += p.solved;
        all.unsolvable += p.unsolvable;
        all.undetermined += p.undetermined;
        all.unsolvableSeeds.insert(all.unsolvableSeeds.end(), p.unsolvableSeeds.begin(), p.unsolvableSeeds.end());
        all.undeterminedSeeds.insert(all.undeterminedSeeds.end(), p.undeterminedSeeds.begin(), p.undeterminedSeeds.end());
    }
    std::sort(all.unsolvableSeeds.begin(), all.unsolvableSeeds.end());
    std::sort(all.undeterminedSeeds.begin(), all.undeterminedSeeds.end());

    long n = all.solved + all.unsolvable + all.undetermined;
    auto pct = [&](long k) { return n ? 100.0 * k / n : 0.0; };
    std::printf("solved:        %ld (%.2f%%)\n", all.solved, pct(all.solved));
    std::printf("unsolvable:    %ld (%.2f%%)\n", all.unsolvable, pct(all.unsolvable));
    std::printf("undetermined:  %ld (%.2f%%)   [hit the %zu-state budget]\n\n", all.undetermined,
                pct(all.undetermined), budget);

    long resolved = all.solved + all.unsolvable;
    Wilson w = wilson(all.solved, resolved);
    std::printf("win rate among resolved deals: %.2f%%  (95%% CI [%.2f%%, %.2f%%])\n",
                resolved ? 100.0 * all.solved / resolved : 0.0, 100.0 * w.lo, 100.0 * w.hi);
    std::printf("overall bounds (undetermined as loss .. win): [%.2f%%, %.2f%%]\n",
                pct(all.solved), pct(all.solved + all.undetermined));
    std::printf("ran in %.1fs (%.2f ms/deal)\n", secs, n ? 1000.0 * secs / n : 0.0);

    auto printSeeds = [](const char* label, const std::vector<uint64_t>& v) {
        if (v.empty()) return;
        std::printf("\n%s (%zu): ", label, v.size());
        for (size_t i = 0; i < v.size() && i < 60; ++i)
            std::printf("%llu%s", (unsigned long long)v[i], i + 1 < v.size() && i < 59 ? "," : "");
        if (v.size() > 60) std::printf(" ...");
        std::printf("\n");
    };
    printSeeds("unsolvable seeds", all.unsolvableSeeds);
    printSeeds("undetermined seeds", all.undeterminedSeeds);
    return 0;
}
