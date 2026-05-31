#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include "icon_data.h"  // embedded window icon (native only)
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "audio.hpp"
#include "game.hpp"
#include "render.hpp"
#include "storage.hpp"

namespace {
constexpr Uint64 kDoubleClickMs = 350;
constexpr float kDealStagger = 0.035f;  // delay between successive dealt cards
constexpr float kDealDur = 0.16f;        // flight time of one dealt card

float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }
bool inRect(float x, float y, const SDL_FRect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
}  // namespace

// Cards visually detached from the board: a drag-in-progress (follows the
// pointer) or a card/run in flight (snap-back, auto-move, forced foundation).
struct Lift {
    bool active = false;
    bool followPointer = false;
    PileKind src = PileKind::Tableau;
    int col = -1;
    std::vector<Card> cards;  // cards[0] is the grabbed/leading card
    float fanY = 0;
    float grabDX = 0, grabDY = 0;
    float px = 0, py = 0;                  // pointer position while dragging
    float fromX = 0, fromY = 0, toX = 0, toY = 0;  // flight endpoints (leading card)
    float t = 0, dur = 0.18f;
    std::function<void()> onComplete;
};

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* sdl = nullptr;
    SDL_Cursor* curArrow = nullptr;
    SDL_Cursor* curHand = nullptr;
    Renderer rr;
    Audio audio;
    Game game;
    Stats stats;
    Layout layout;
    float outW = 0, outH = 0;  // render-target size used for the current layout

    Lift lift;
    bool won = false;
    bool gameDirty = false;  // game changed since last save; persisted once settled
    Uint64 lastTick = 0;

    // Deal animation: cards fly from the stock to their tableau slots, staggered.
    bool dealing = false;
    float dealStart = 0, dealEnd = 0;
    float dealRevealAt[7][7]{};  // [col][row] = seconds after dealStart to begin

    // Draw animation: the newly drawn cards fly from the stock into the waste.
    bool drawing = false;
    float drawStart = 0, drawEnd = 0;
    int drawFrom = 0;  // first waste index that is animating in

    float nowSec() const { return SDL_GetTicks() / 1000.0f; }

    // pending pointer interaction
    bool havePress = false;
    PileKind pressKind = PileKind::Tableau;
    int pressCol = -1;
    int pressIdx = -1;
    float pressX = 0, pressY = 0;
    Uint64 lastClickTime = 0;
    float lastClickX = 0, lastClickY = 0;

    // --- helpers ---
    // On the web the canvas follows the viewport via CSS, but SDL only learns
    // the new drawable size if we resize the window to match. Keep them synced
    // each frame so the layout adapts to browser/orientation changes.
    void syncWindow() {
#ifdef __EMSCRIPTEN__
        int w = EM_ASM_INT({ return Math.floor(window.innerWidth); });
        int h = EM_ASM_INT({ return Math.floor(window.innerHeight); });
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window, &ww, &wh);
        if (w > 0 && h > 0 && (w != ww || h != wh)) SDL_SetWindowSize(window, w, h);
#endif
    }

    void refreshLayout() {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(sdl, &w, &h);
        outW = (float)w;
        outH = (float)h;
        layout = computeLayout((float)w, (float)h, (int)game.waste.size());
    }

    SDL_FRect wasteTopRect() const {
        return wasteCardRect(layout, std::max(0, (int)game.waste.size() - 1));
    }

    void setMuted(bool m) {
        stats.muted = m;
        audio.setMuted(m);
        saveStats(stats);
    }

    void persist() {
        saveGame(game.serialize());
        gameDirty = false;
    }

    void redeal() {
        game.dealWinnable();
        won = false;
        drawing = false;
        lift = Lift{};
        havePress = false;
        startDealAnim();
        persist();  // save the new deal immediately so a refresh resumes it
    }

    void startDealAnim() {
        audio.play(Sfx::Shuffle);  // every deal (initial + re-deal) gets the shuffle
        dealing = true;
        dealStart = nowSec();
        int k = 0;
        // Reveal in row-major order for a staggered diagonal cascade.
        for (int row = 0; row < 7; ++row)
            for (int col = row; col < 7; ++col) dealRevealAt[col][row] = (k++) * kDealStagger;
        dealEnd = (k - 1) * kDealStagger + kDealDur;
    }

    void startDrawAnim(int n) {
        drawing = true;
        drawStart = nowSec();
        drawFrom = (int)game.waste.size() - n;  // the n newest waste cards animate in
        drawEnd = (n - 1) * kDealStagger + kDealDur;
    }

    // Hit-test the board (not UI). Fills press* and returns true on a grabbable card.
    bool hitBoard(float x, float y) {
        if (game.freecellUnlocked && game.freecell && inRect(x, y, layout.stock)) {
            pressKind = PileKind::FreeCell;
            pressCol = -1;
            pressIdx = 0;
            return true;
        }
        if (!game.waste.empty() && inRect(x, y, wasteTopRect())) {
            pressKind = PileKind::Waste;
            pressCol = -1;
            pressIdx = (int)game.waste.size() - 1;
            return true;
        }
        for (int col = 0; col < 7; ++col) {
            const SDL_FRect& rc = layout.tableau[col];
            int n = (int)game.tableau[col].size();
            if (n == 0) continue;
            if (x < rc.x || x >= rc.x + layout.cardW) continue;
            float lastBottom = rc.y + (n - 1) * layout.fanY + layout.cardH;
            if (y < rc.y || y > lastBottom) continue;
            int i = (int)std::floor((y - rc.y) / layout.fanY);
            i = std::clamp(i, 0, n - 1);
            if (!game.grabbable(col, i)) continue;
            pressKind = PileKind::Tableau;
            pressCol = col;
            pressIdx = i;
            return true;
        }
        return false;
    }

    void beginDrag() {
        SDL_FRect cardRect;
        if (pressKind == PileKind::Tableau) {
            if (!game.grabbable(pressCol, pressIdx)) return;
            cardRect = tableauCardRect(layout, pressCol, pressIdx);
            auto& pile = game.tableau[pressCol];
            lift.cards.assign(pile.begin() + pressIdx, pile.end());
            pile.erase(pile.begin() + pressIdx, pile.end());
            lift.fanY = layout.fanY;
        } else if (pressKind == PileKind::Waste) {
            cardRect = wasteTopRect();
            lift.cards = {game.waste.back()};
            game.waste.pop_back();
            lift.fanY = 0;
        } else {  // FreeCell
            cardRect = layout.stock;
            lift.cards = {*game.freecell};
            game.freecell.reset();
            lift.fanY = 0;
        }
        lift.active = true;
        lift.followPointer = true;
        lift.src = pressKind;
        lift.col = pressCol;
        lift.grabDX = pressX - cardRect.x;
        lift.grabDY = pressY - cardRect.y;
        lift.px = pressX;
        lift.py = pressY;
        audio.play(Sfx::Pickup);
    }

    void resolveDrop() {
        float lx = lift.px - lift.grabDX, ly = lift.py - lift.grabDY;
        float cx = lx + layout.cardW * 0.5f, cy = ly + layout.cardH * 0.5f;

        bool valid = false;
        // Drag a single foundation-ready card onto any foundation slot.
        if (lift.cards.size() == 1 && game.foundationReady(lift.cards[0])) {
            for (int i = 0; i < 4 && !valid; ++i)
                if (inRect(cx, cy, layout.foundations[i])) {
                    game.foundation[(int)lift.cards[0].suit] = lift.cards[0].rank;
                    valid = true;
                }
        }
        if (!valid && lift.cards.size() == 1 && game.freecellUnlocked && !game.freecell &&
            inRect(cx, cy, layout.stock)) {
            game.freecell = lift.cards[0];
            valid = true;
        }
        if (!valid) {
            int dest = -1;
            for (int col = 0; col < 7; ++col)
                if (cx >= layout.tableau[col].x && cx < layout.tableau[col].x + layout.cardW) dest = col;
            if (dest >= 0 && game.canDropOnColumn(lift.cards[0], dest)) {
                for (auto& c : lift.cards) game.tableau[dest].push_back(c);
                valid = true;
            }
        }

        if (valid) {
            audio.play(Sfx::Drop);
            lift.active = false;
            lift.cards.clear();
            gameDirty = true;
            maybeAutoMove();
            checkWin();
        } else {
            snapBack();
        }
    }

    void snapBack() {
        float fx = lift.px - lift.grabDX, fy = lift.py - lift.grabDY;
        float tx = fx, ty = fy;
        if (lift.src == PileKind::Tableau) {
            tx = layout.tableau[lift.col].x;
            ty = layout.tableau[lift.col].y + (int)game.tableau[lift.col].size() * layout.fanY;
        } else if (lift.src == PileKind::Waste) {
            // Card was popped on pickup; it returns to the end of the waste.
            SDL_FRect r = wasteCardRect(layout, (int)game.waste.size());
            tx = r.x;
            ty = r.y;
        } else {
            tx = layout.stock.x;
            ty = layout.stock.y;
        }
        lift.followPointer = false;
        lift.fromX = fx;
        lift.fromY = fy;
        lift.toX = tx;
        lift.toY = ty;
        lift.t = 0;
        lift.dur = 0.16f;
        auto cards = lift.cards;
        PileKind src = lift.src;
        int col = lift.col;
        lift.onComplete = [this, cards, src, col]() {
            if (src == PileKind::Tableau)
                for (auto& c : cards) game.tableau[col].push_back(c);
            else if (src == PileKind::Waste)
                game.waste.push_back(cards[0]);
            else
                game.freecell = cards[0];
        };
    }

    void startFlight(Card c, float fx, float fy, float tx, float ty, std::function<void()> done) {
        lift.active = true;
        lift.followPointer = false;
        lift.cards = {c};
        lift.fanY = 0;
        lift.fromX = fx;
        lift.fromY = fy;
        lift.toX = tx;
        lift.toY = ty;
        lift.t = 0;
        lift.dur = 0.18f;
        lift.onComplete = std::move(done);
        audio.play(Sfx::Swoosh);
    }

    void maybeAutoMove() {
        if (lift.active || dealing || drawing) return;
        auto mv = game.findAutoMove();
        if (!mv) return;
        Card c = mv->card;
        float fx, fy;
        if (mv->kind == PileKind::Tableau) {
            int n = (int)game.tableau[mv->col].size();
            SDL_FRect rc = tableauCardRect(layout, mv->col, n - 1);
            fx = rc.x;
            fy = rc.y;
            game.tableau[mv->col].pop_back();
        } else if (mv->kind == PileKind::Waste) {
            SDL_FRect rc = wasteTopRect();
            fx = rc.x;
            fy = rc.y;
            game.waste.pop_back();
        } else {
            fx = layout.stock.x;
            fy = layout.stock.y;
            game.freecell.reset();
        }
        SDL_FRect dst = layout.foundations[(int)c.suit];
        int suit = (int)c.suit;
        startFlight(c, fx, fy, dst.x, dst.y, [this, suit, c]() { game.foundation[suit] = c.rank; });
    }

    void tryForce() {
        Card c;
        float fx, fy;
        if (pressKind == PileKind::Tableau) {
            auto& pile = game.tableau[pressCol];
            if (pile.empty() || pressIdx != (int)pile.size() - 1) return;
            c = pile.back();
            if (!game.foundationReady(c)) return;
            SDL_FRect rc = tableauCardRect(layout, pressCol, pressIdx);
            fx = rc.x;
            fy = rc.y;
            pile.pop_back();
        } else if (pressKind == PileKind::Waste) {
            if (game.waste.empty()) return;
            c = game.waste.back();
            if (!game.foundationReady(c)) return;
            SDL_FRect rc = wasteTopRect();
            fx = rc.x;
            fy = rc.y;
            game.waste.pop_back();
        } else {
            if (!game.freecell) return;
            c = *game.freecell;
            if (!game.foundationReady(c)) return;
            fx = layout.stock.x;
            fy = layout.stock.y;
            game.freecell.reset();
        }
        SDL_FRect dst = layout.foundations[(int)c.suit];
        int suit = (int)c.suit;
        startFlight(c, fx, fy, dst.x, dst.y, [this, suit, c]() { game.foundation[suit] = c.rank; });
    }

    void checkWin() {
        if (game.won() && !won) {
            won = true;
            stats.wins++;
            saveStats(stats);
            clearGame();  // a finished game shouldn't be resumed
            gameDirty = false;
        }
    }

    void onPointerDown(float x, float y) {
        if (won) {  // win overlay is a full-screen modal: tap anywhere to play again
            redeal();
            return;
        }
        if (inRect(x, y, layout.redealBtn)) {
            redeal();
            return;
        }
        if (inRect(x, y, layout.muteBtn)) {
            setMuted(!stats.muted);
            return;
        }
        if (dealing || drawing || (lift.active && !lift.followPointer)) return;  // animating

        if (!game.stock.empty() && inRect(x, y, layout.stock)) {
            int n = game.draw3();
            if (n > 0) {
                audio.play(Sfx::Flip);
                startDrawAnim(n);  // auto-mover runs once the cards finish flying in
                gameDirty = true;
            }
            return;
        }

        pressX = x;
        pressY = y;
        havePress = hitBoard(x, y);
    }

    void setHoverCursor(bool hand) {
#ifdef __EMSCRIPTEN__
        EM_ASM({ Module.canvas.style.cursor = $0 ? 'pointer' : 'default'; }, hand);
#else
        if (SDL_Cursor* w = hand ? curHand : curArrow) SDL_SetCursor(w);
#endif
    }

    void onPointerMove(float x, float y) {
        // Hand cursor over the buttons (or anywhere while the win modal is up).
        bool overBtn = inRect(x, y, layout.redealBtn) || inRect(x, y, layout.muteBtn);
        setHoverCursor(won || (overBtn && !lift.active));

        if (lift.active && lift.followPointer) {
            lift.px = x;
            lift.py = y;
            return;
        }
        if (havePress && !lift.active) {
            float d = std::hypot(x - pressX, y - pressY);
            if (d > layout.cardW * 0.12f) beginDrag();
        }
    }

    void onPointerUp(float x, float y) {
        if (lift.active && lift.followPointer) {
            resolveDrop();
            havePress = false;
            return;
        }
        if (havePress) {  // a tap, not a drag
            Uint64 now = SDL_GetTicks();
            bool dbl = (now - lastClickTime < kDoubleClickMs) &&
                       std::hypot(x - lastClickX, y - lastClickY) < layout.cardW;
            if (dbl) {
                tryForce();
                lastClickTime = 0;
            } else {
                lastClickTime = now;
                lastClickX = x;
                lastClickY = y;
            }
        }
        havePress = false;
    }

    // --- drawing ---
    void drawButton(const SDL_FRect& r, const char* label) {
        rr.fillRoundedRect(r, r.h * 0.25f, rgba(34, 120, 92));
        float s = layout.uiTextPx;
        float maxW = r.w * 0.84f;
        float tw = rr.textWidth(s, label);
        if (tw > maxW) {
            s *= maxW / tw;
            tw = maxW;
        }
        float th = rr.textHeight(s);
        rr.drawText(r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, s, rgba(240, 248, 244), label);
    }

    void draw() {
        rr.clear(rgba(11, 84, 62));

        // Foundations (with a faint suit watermark when empty).
        for (int i = 0; i < 4; ++i) {
            if (game.foundation[i] > 0) {
                rr.drawCard(layout.foundations[i], Card{(Suit)i, game.foundation[i]});
            } else {
                // Distinct dark panel (not the table green) with a muted suit mark.
                // The mark is opaque: a translucent suit would double-blend where
                // its sub-shapes (circles/stem) overlap and reveal internal seams.
                SDL_FRect f = layout.foundations[i];
                rr.fillRoundedRect(f, f.w * 0.12f, rgba(22, 48, 40));
                rr.drawSuit((Suit)i, f.x + f.w * 0.5f, f.y + f.h * 0.5f, f.h * 0.24f,
                            rgba(79, 99, 91));
            }
        }

        // Stock (with thickness for draws left) / waste / free cell.
        if (!game.stock.empty()) {
            rr.drawDeck(layout.stock, (int)game.stock.size());
        } else if (game.freecellUnlocked) {
            rr.drawSlot(layout.stock, true);
            if (game.freecell) rr.drawCard(layout.stock, *game.freecell);
        } else {
            rr.drawSlot(layout.stock);
        }
        // Waste: the entire pile spills rightward (wrapping to a 2nd row) so every
        // card can be read.
        rr.drawSlot(layout.waste);
        const float drawEl = nowSec() - drawStart;
        for (int k = 0; k < (int)game.waste.size(); ++k) {
            SDL_FRect dst = wasteCardRect(layout, k);
            if (drawing && k >= drawFrom) {
                float reveal = (k - drawFrom) * kDealStagger;
                if (drawEl < reveal) continue;  // still in the stock
                if (drawEl < reveal + kDealDur) {
                    float u = smoothstep((drawEl - reveal) / kDealDur);
                    dst.x = lerp(layout.stock.x, dst.x, u);
                    dst.y = lerp(layout.stock.y, dst.y, u);
                }
            }
            rr.drawCard(dst, game.waste[k]);
        }

        // Tableau (during the deal each card flies in from the stock).
        const float el = nowSec() - dealStart;
        for (int col = 0; col < 7; ++col) {
            int n = (int)game.tableau[col].size();
            if (n == 0) rr.drawSlot(layout.tableau[col]);
            for (int i = 0; i < n; ++i) {
                SDL_FRect slot = tableauCardRect(layout, col, i);
                if (dealing && i <= col) {
                    float reveal = dealRevealAt[col][i];
                    if (el < reveal) continue;  // not dealt yet
                    if (el < reveal + kDealDur) {
                        float u = smoothstep((el - reveal) / kDealDur);
                        slot.x = lerp(layout.stock.x, slot.x, u);
                        slot.y = lerp(layout.stock.y, slot.y, u);
                    }
                }
                rr.drawCard(slot, game.tableau[col][i]);
            }
        }

        // UI: wins counter (top-right), buttons.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d wins", stats.wins);
        float ws = layout.uiTextPx;
        rr.drawText(layout.winsAnchor.x - rr.textWidth(ws, buf), layout.winsAnchor.y, ws,
                    rgba(245, 245, 235), buf);
        drawButton(layout.redealBtn, "Re-deal");
        rr.fillRoundedRect(layout.muteBtn, layout.muteBtn.h * 0.25f, rgba(34, 120, 92));
        rr.drawSpeaker(layout.muteBtn, stats.muted, rgba(240, 248, 244));

        // Lift (dragged or in-flight cards) on top.
        if (lift.active) {
            float bx, by;
            if (lift.followPointer) {
                bx = lift.px - lift.grabDX;
                by = lift.py - lift.grabDY;
            } else {
                float u = smoothstep(lift.t / lift.dur);
                bx = lerp(lift.fromX, lift.toX, u);
                by = lerp(lift.fromY, lift.toY, u);
            }
            for (size_t k = 0; k < lift.cards.size(); ++k)
                rr.drawCard(SDL_FRect{bx, by + k * lift.fanY, layout.cardW, layout.cardH}, lift.cards[k]);
        }

        // Win overlay.
        if (won) {
            float w = outW, h = outH;
            rr.fillRect(SDL_FRect{0, 0, w, h}, rgba(0, 0, 0, 150));
            float s = std::max(24.0f, layout.cardH * 0.42f);
            const char* msg = "You win!";
            rr.drawText((w - rr.textWidth(s, msg)) * 0.5f, h * 0.5f - rr.textHeight(s),
                        s, rgba(255, 230, 130), msg);
            const char* sub = "Tap to play again";
            float s2 = std::max(12.0f, layout.cardH * 0.20f);
            rr.drawText((w - rr.textWidth(s2, sub)) * 0.5f, h * 0.5f + rr.textHeight(s) * 0.6f,
                        s2, rgba(230, 230, 230), sub);
        }

        SDL_RenderPresent(sdl);
    }

    void tick() {
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTick) / 1000.0f;
        lastTick = now;
        if (dt > 0.1f) dt = 0.1f;

        audio.update();

        if (dealing && nowSec() - dealStart > dealEnd) {
            dealing = false;
            maybeAutoMove();  // nothing eligible at deal time, but stay consistent
        }
        if (drawing && nowSec() - drawStart > drawEnd) {
            drawing = false;
            maybeAutoMove();  // a newly drawn top card may now auto-advance
            checkWin();
        }

        if (lift.active && !lift.followPointer) {
            lift.t += dt;
            if (lift.t >= lift.dur) {
                auto cb = std::move(lift.onComplete);
                lift.onComplete = nullptr;
                lift.active = false;
                lift.cards.clear();
                if (cb) cb();
                gameDirty = true;
                maybeAutoMove();
                checkWin();
            }
        }

        // Persist the game once it settles (no drag/animation/deal in flight).
        if (gameDirty && !lift.active && !dealing && !drawing && !won) {
            persist();
        }
        draw();
    }
};

// --- SDL callback entry points ---
SDL_AppResult SDL_AppInit(void** appstate, int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    App* app = new App();
    *appstate = app;

    if (!SDL_CreateWindowAndRenderer("Sawayama Solitaire", 1000, 720,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &app->window, &app->sdl)) {
        SDL_Log("CreateWindowAndRenderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->sdl, 1);
#ifndef __EMSCRIPTEN__
    if (SDL_Surface* ic = SDL_CreateSurfaceFrom(kIconW, kIconH, SDL_PIXELFORMAT_RGBA32,
                                                (void*)kIconRGBA, kIconW * 4)) {
        SDL_SetWindowIcon(app->window, ic);
        SDL_DestroySurface(ic);
    }
#endif
    app->rr.init(app->sdl);
    app->curArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    app->curHand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    app->audio.init();
    app->stats = loadStats();
    app->audio.setMuted(app->stats.muted);
    app->lastTick = SDL_GetTicks();
    app->syncWindow();
    // Resume a saved in-progress game if there is one; otherwise deal a fresh
    // game with the opening cascade.
    std::string saved = loadGame();
    if (!saved.empty() && app->game.deserialize(saved) && !app->game.won()) {
        app->dealing = false;
    } else {
        app->game.dealWinnable();  // fresh, proven-winnable opening
        app->startDealAnim();
        app->persist();  // save the opening deal so a refresh resumes it
    }
    app->refreshLayout();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    App* app = static_cast<App*>(appstate);
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button == SDL_BUTTON_LEFT) {
                SDL_ConvertEventToRenderCoordinates(app->sdl, event);
                app->refreshLayout();
                app->onPointerDown(event->button.x, event->button.y);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button == SDL_BUTTON_LEFT) {
                SDL_ConvertEventToRenderCoordinates(app->sdl, event);
                app->onPointerUp(event->button.x, event->button.y);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ConvertEventToRenderCoordinates(app->sdl, event);
            app->onPointerMove(event->motion.x, event->motion.y);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.key == SDLK_R) app->redeal();
            else if (event->key.key == SDLK_M) app->setMuted(!app->stats.muted);
            else if (event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
            break;
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = static_cast<App*>(appstate);
    app->syncWindow();
    app->refreshLayout();
    app->tick();
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    App* app = static_cast<App*>(appstate);
    if (app) {
        app->audio.shutdown();
        app->rr.shutdown();  // free the font atlas before destroying the renderer
        if (app->curArrow) SDL_DestroyCursor(app->curArrow);
        if (app->curHand) SDL_DestroyCursor(app->curHand);
        if (app->sdl) SDL_DestroyRenderer(app->sdl);
        if (app->window) SDL_DestroyWindow(app->window);
        delete app;
    }
}
