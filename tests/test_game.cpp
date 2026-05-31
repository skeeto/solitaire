// Standalone logic checks for the rules engine (no SDL). Build:
//   c++ -std=c++20 tests/test_game.cpp src/game.cpp -I src -o /tmp/tgame && /tmp/tgame
#include <cassert>
#include <cstdio>

#include "../src/game.hpp"

static int checks = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        ++checks;                                                        \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            return 1;                                                    \
        }                                                                \
    } while (0)

static Card C(Suit s, int r) { return Card{s, r}; }

int main() {
    Game g;  // constructor deals

    // --- deal integrity, run many shuffles ---
    for (int iter = 0; iter < 2000; ++iter) {
        g.deal();
        int total = (int)g.stock.size() + (int)g.waste.size();
        for (int c = 0; c < 7; ++c) {
            CHECK((int)g.tableau[c].size() == c + 1);
            total += (int)g.tableau[c].size();
            // no Ace exposed at the bottom (playable) position of any column
            CHECK(g.tableau[c].back().rank != 1);
        }
        CHECK(total == 52);
        CHECK((int)g.stock.size() == 24);
        CHECK(g.waste.empty());
        CHECK(!g.freecellUnlocked);
    }

    // --- deterministic, portable deal: a seed reproduces an identical deal ---
    {
        auto sameDeal = [](const Game& x, const Game& y) {
            for (int c = 0; c < 7; ++c) {
                if (x.tableau[c].size() != y.tableau[c].size()) return false;
                for (size_t i = 0; i < x.tableau[c].size(); ++i)
                    if (x.tableau[c][i].suit != y.tableau[c][i].suit ||
                        x.tableau[c][i].rank != y.tableau[c][i].rank)
                        return false;
            }
            if (x.stock.size() != y.stock.size()) return false;
            for (size_t i = 0; i < x.stock.size(); ++i)
                if (x.stock[i].suit != y.stock[i].suit || x.stock[i].rank != y.stock[i].rank)
                    return false;
            return true;
        };
        Game p, q;
        p.rng.seed(123456789ULL);
        q.rng.seed(123456789ULL);
        p.deal();
        q.deal();
        CHECK(sameDeal(p, q));   // same seed -> identical deal
        Game r;
        r.rng.seed(987654321ULL);
        r.deal();
        CHECK(!sameDeal(p, r));  // different seed -> different deal
    }

    // --- draw-three single pass + free-cell unlock ---
    g.deal();
    CHECK(g.draw3() == 3);
    CHECK(g.waste.size() == 3 && g.stock.size() == 21 && !g.freecellUnlocked);
    int guard = 0;
    while (!g.stock.empty() && guard++ < 100) g.draw3();
    CHECK(g.stock.empty());
    CHECK(g.waste.size() == 24);
    CHECK(g.freecellUnlocked);
    CHECK(g.draw3() == 0);  // single pass: nothing comes back

    // --- tableau stacking rules ---
    CHECK(Game::canStackOn(C(Suit::Hearts, 6), C(Suit::Spades, 7)));    // red on black, n-1
    CHECK(Game::canStackOn(C(Suit::Clubs, 9), C(Suit::Diamonds, 10)));  // black on red
    CHECK(!Game::canStackOn(C(Suit::Hearts, 6), C(Suit::Diamonds, 7))); // same color
    CHECK(!Game::canStackOn(C(Suit::Hearts, 5), C(Suit::Spades, 7)));   // wrong rank

    // --- conservative auto-mover threshold ---
    Game a;
    for (auto& t : a.tableau) t.clear();
    a.stock.clear();
    a.waste.clear();
    a.foundation = {0, 0, 0, 0};

    // Ace is always eligible once its foundation is ready (here, empty).
    CHECK(a.autoEligible(C(Suit::Hearts, 1)));

    // A two is always eligible once its ace is up, even while the opposite-color
    // aces are still in play -- the only thing a two holds is an ace, which
    // always auto-advances.
    a.foundation = {0, 0, 1, 0};  // S=1 (2S ready); red aces NOT up yet
    CHECK(a.foundationReady(C(Suit::Spades, 2)));
    CHECK(a.minOppositeColorInPlay(Suit::Spades) == 1);  // red aces in play
    CHECK(a.autoEligible(C(Suit::Spades, 2)));
    // ...but a two whose own ace isn't up yet still can't go (foundation rule).
    a.foundation = {0, 0, 0, 0};
    CHECK(!a.autoEligible(C(Suit::Spades, 2)));

    // Black 3 ready on its foundation, with red 2s still in play -> must stay.
    a.foundation = {1, 1, 2, 0};  // H=1, D=1 (so red 2s in play), S=2 (3S ready)
    CHECK(a.foundationReady(C(Suit::Spades, 3)));
    CHECK(a.minOppositeColorInPlay(Suit::Spades) == 2);
    CHECK(!a.autoEligible(C(Suit::Spades, 3)));  // a red 2 could still use the 3

    // Once both red 2s are up, the black 3 is safe to advance.
    a.foundation = {2, 2, 2, 0};
    CHECK(a.minOppositeColorInPlay(Suit::Spades) == 3);
    CHECK(a.autoEligible(C(Suit::Spades, 3)));

    // No opposite-color cards left -> always safe.
    a.foundation = {13, 13, 5, 0};
    CHECK(a.autoEligible(C(Suit::Spades, 6)));

    // --- findAutoMove picks up an eligible exposed card ---
    Game b;
    for (auto& t : b.tableau) t.clear();
    b.stock.clear();
    b.waste.clear();
    b.foundation = {0, 0, 0, 0};
    b.tableau[0].push_back(C(Suit::Diamonds, 1));  // exposed Ace
    auto mv = b.findAutoMove();
    CHECK(mv.has_value());
    CHECK(mv->kind == PileKind::Tableau && mv->col == 0 && mv->card.rank == 1);

    // --- empty column accepts anything; non-empty enforces the rule ---
    Game d;
    for (auto& t : d.tableau) t.clear();
    d.tableau[1].push_back(C(Suit::Spades, 7));
    CHECK(d.canDropOnColumn(C(Suit::Hearts, 5), 0));   // empty column
    CHECK(d.canDropOnColumn(C(Suit::Hearts, 6), 1));   // 6H on 7S
    CHECK(!d.canDropOnColumn(C(Suit::Spades, 6), 1));  // 6S on 7S (same color)

    // --- grabbable run detection ---
    Game e;
    for (auto& t : e.tableau) t.clear();
    e.tableau[0] = {C(Suit::Spades, 8), C(Suit::Hearts, 7), C(Suit::Spades, 6)};  // valid run
    CHECK(e.grabbable(0, 0));
    CHECK(e.grabbable(0, 1));
    e.tableau[1] = {C(Suit::Spades, 8), C(Suit::Hearts, 9)};  // not descending
    CHECK(e.grabbable(1, 1));   // single top card always grabbable
    CHECK(!e.grabbable(1, 0));  // 8S,9H is not an ordered run

    // --- serialize / deserialize round-trip ---
    Game src;
    src.deal();
    src.draw3();
    src.foundation = {3, 0, 1, 2};
    src.freecellUnlocked = true;
    src.freecell = C(Suit::Spades, 9);
    std::string blob = src.serialize();
    Game dst;
    CHECK(dst.deserialize(blob));
    CHECK(dst.foundation == src.foundation);
    CHECK(dst.freecellUnlocked && dst.freecell && dst.freecell->rank == 9 &&
          dst.freecell->suit == Suit::Spades);
    CHECK(dst.stock.size() == src.stock.size());
    CHECK(dst.waste.size() == src.waste.size());
    for (int i = 0; i < 7; ++i) CHECK(dst.tableau[i].size() == src.tableau[i].size());
    CHECK(dst.serialize() == blob);     // stable round-trip
    CHECK(!dst.deserialize("garbage"));  // malformed input rejected

    std::printf("OK: %d checks passed\n", checks);
    return 0;
}
