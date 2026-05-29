#include "game.hpp"

#include <algorithm>
#include <limits>

Game::Game() {
    rng.seed(std::random_device{}());
    deal();
}

void Game::deal() {
    std::vector<Card> deck;
    deck.reserve(52);
    for (int s = 0; s < 4; ++s)
        for (int r = 1; r <= 13; ++r) deck.push_back(Card{static_cast<Suit>(s), r});

    // Reshuffle until no column's exposed (bottom-most, playable) card is an Ace,
    // matching the original's deal generator avoiding exposed opening Aces.
    for (;;) {
        std::shuffle(deck.begin(), deck.end(), rng);
        bool exposedAce = false;
        // Columns get 1..7 cards; the last dealt card of a column is the exposed one.
        int idx = 0;
        for (int col = 0; col < 7 && !exposedAce; ++col) {
            idx += (col + 1);
            if (deck[idx - 1].rank == 1) exposedAce = true;
        }
        if (!exposedAce) break;
    }

    for (auto& c : tableau) c.clear();
    stock.clear();
    waste.clear();
    foundation = {0, 0, 0, 0};
    freecell.reset();
    freecellUnlocked = false;

    int idx = 0;
    for (int col = 0; col < 7; ++col)
        for (int n = 0; n <= col; ++n) tableau[col].push_back(deck[idx++]);
    while (idx < 52) stock.push_back(deck[idx++]);
}

int Game::draw3() {
    int n = 0;
    for (; n < 3 && !stock.empty(); ++n) {
        waste.push_back(stock.back());
        stock.pop_back();
    }
    if (stock.empty()) freecellUnlocked = true;
    return n;
}

bool Game::won() const {
    return foundation[0] == 13 && foundation[1] == 13 && foundation[2] == 13 && foundation[3] == 13;
}

bool Game::canStackOn(Card moving, Card onto) {
    return moving.rank == onto.rank - 1 && isRed(moving.suit) != isRed(onto.suit);
}

bool Game::grabbable(int col, int idx) const {
    const auto& pile = tableau[col];
    if (idx < 0 || idx >= static_cast<int>(pile.size())) return false;
    for (int i = idx; i + 1 < static_cast<int>(pile.size()); ++i)
        if (!canStackOn(pile[i + 1], pile[i])) return false;
    return true;
}

bool Game::canDropOnColumn(Card first, int col) const {
    const auto& pile = tableau[col];
    if (pile.empty()) return true;  // any card or run may fill an empty column
    return canStackOn(first, pile.back());
}

bool Game::foundationReady(Card c) const {
    return foundation[static_cast<int>(c.suit)] == c.rank - 1;
}

int Game::minOppositeColorInPlay(Suit s) const {
    int best = std::numeric_limits<int>::max();
    bool red = isRed(s);
    for (int i = 0; i < 4; ++i) {
        Suit os = static_cast<Suit>(i);
        if (isRed(os) == red) continue;       // same color, skip
        if (foundation[i] >= 13) continue;     // that suit fully on foundation
        best = std::min(best, foundation[i] + 1);  // smallest such card still in play
    }
    return best;
}

bool Game::autoEligible(Card c) const {
    if (!foundationReady(c)) return false;
    if (c.rank == 1) return true;  // Aces have no holding utility
    int m = minOppositeColorInPlay(c.suit);
    if (m == std::numeric_limits<int>::max()) return true;  // nothing left to hold
    // Keep this card while an opposite-color card of rank (c.rank - 1) is still
    // in play (it could be stacked on this card); otherwise it is safe to send up.
    return c.rank <= m;
}

std::optional<AutoSource> Game::findAutoMove() const {
    for (int col = 0; col < 7; ++col)
        if (!tableau[col].empty() && autoEligible(tableau[col].back()))
            return AutoSource{PileKind::Tableau, col, tableau[col].back()};
    if (!waste.empty() && autoEligible(waste.back()))
        return AutoSource{PileKind::Waste, -1, waste.back()};
    if (freecell && autoEligible(*freecell))
        return AutoSource{PileKind::FreeCell, -1, *freecell};
    return std::nullopt;
}
