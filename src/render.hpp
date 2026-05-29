// Adaptive layout + asset-free procedural drawing (rounded-rect cards, geometric
// suit pips, SDL debug-font text). The scene is composed by main.cpp from these
// primitives so this file stays free of interaction state.
#pragma once

#include <SDL3/SDL.h>

#include "game.hpp"

struct Layout {
    bool vertical = false;
    float cardW = 0, cardH = 0;
    float fanY = 0;      // vertical offset between fanned tableau cards
    float wasteFan = 0;  // horizontal offset between fanned waste cards

    SDL_FRect stock{};       // doubles as the free-cell slot once unlocked
    SDL_FRect waste{};       // origin (card 0) of the waste fan
    int wastePerRow = 1;     // cards per row before wrapping
    float wasteRowStep = 0;  // vertical offset between wrapped waste rows
    SDL_FRect foundations[4]{};
    SDL_FRect tableau[7]{};  // top-card rect of each column

    SDL_FRect redealBtn{};
    SDL_FRect muteBtn{};
    SDL_FRect winsAnchor{};  // top-right point for the wins text (w=h=0)
    float uiTextScale = 2.0f;
};

Layout computeLayout(float w, float h, int wasteCount);

// Rect of the card at depth `i` (0 = bottom) in tableau column `col`.
SDL_FRect tableauCardRect(const Layout& L, int col, int i);

// Rect of waste card `k` (0 = first drawn), accounting for rightward fan + wrap.
SDL_FRect wasteCardRect(const Layout& L, int k);

inline SDL_FColor rgba(int r, int g, int b, int a = 255) {
    return SDL_FColor{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

const char* rankString(int rank);

class Renderer {
public:
    bool init(SDL_Renderer* r);

    void clear(SDL_FColor c);
    void fillRect(SDL_FRect rc, SDL_FColor c);
    void fillRoundedRect(SDL_FRect rc, float radius, SDL_FColor c);

    void drawCard(SDL_FRect rc, Card card, bool highlight = false);
    void drawCardBack(SDL_FRect rc);
    void drawDeck(SDL_FRect rc, int cardsLeft);  // card back with a thickness proportional to draws left
    void drawSlot(SDL_FRect rc, bool freecell = false);
    void drawSuit(Suit s, float cx, float cy, float size);
    void drawSuit(Suit s, float cx, float cy, float size, SDL_FColor color);

    void drawText(float x, float y, float scale, SDL_FColor c, const char* str);
    float textWidth(float scale, const char* str) const;
    float textHeight(float scale) const;

    void drawSpeaker(SDL_FRect rc, bool muted, SDL_FColor c);

    SDL_Renderer* sdl() const { return r_; }

private:
    void drawCircle(float cx, float cy, float radius, SDL_FColor c);
    void fillConvex(const SDL_FPoint* pts, int n, SDL_FColor c);

    SDL_Renderer* r_ = nullptr;
};
