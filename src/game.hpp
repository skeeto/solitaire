// Core Sawayama Solitaire model and rules. Deliberately free of SDL so the
// logic can be reasoned about (and unit-tested) on its own.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

enum class Suit : int { Hearts = 0, Diamonds = 1, Spades = 2, Clubs = 3 };

inline bool isRed(Suit s) { return s == Suit::Hearts || s == Suit::Diamonds; }

struct Card {
    Suit suit;
    int rank;  // 1 = Ace ... 13 = King
};

// Which physical pile a card lives in (used for moves and animations).
enum class PileKind { Tableau, Waste, Stock, Foundation, FreeCell };

// Description of a card the auto-mover (or a forced double-click) wants to send
// to its foundation, including where it currently sits.
struct AutoSource {
    PileKind kind;  // Tableau, Waste, or FreeCell
    int col;        // tableau column when kind == Tableau, else unused
    Card card;
};

struct Game {
    std::array<std::vector<Card>, 7> tableau;
    std::vector<Card> stock;
    std::vector<Card> waste;
    std::array<int, 4> foundation{};  // top rank present per suit, 0 = empty
    std::optional<Card> freecell;
    bool freecellUnlocked = false;

    std::mt19937_64 rng;

    Game();

    // Shuffle and deal a fresh game, filtering out openings that expose an Ace.
    void deal();

    // Draw up to three cards from the stock to the waste (single pass). Unlocks
    // the free cell once the stock is exhausted. Returns how many were drawn.
    int draw3();

    bool won() const;

    // --- tableau move queries ---
    static bool canStackOn(Card moving, Card onto);
    // Is the suffix [idx, end) of column `col` a valid descending/alt-color run?
    bool grabbable(int col, int idx) const;
    // May `first` (the bottom card of a moving run) land on tableau column `col`?
    bool canDropOnColumn(Card first, int col) const;

    // --- foundation / auto-mover ---
    bool foundationReady(Card c) const;  // foundation[suit] == rank - 1
    // Smallest opposite-color rank still in play (not yet on a foundation), or
    // INT_MAX if no opposite-color card remains.
    int minOppositeColorInPlay(Suit s) const;
    // Conservative auto-advance test: foundation-ready AND (Ace OR no smaller
    // opposite-color card can still use this card to hold it).
    bool autoEligible(Card c) const;
    // Find the next card the conservative auto-mover should send up, if any.
    std::optional<AutoSource> findAutoMove() const;
};
