#include "tutorial.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr float kAspect = 1.42f;  // card height / width, matching render.cpp

// The palette is borrowed from the board so the panel reads as part of the game:
// the panel body is the empty-foundation dark, the title is the win-overlay gold,
// and the buttons are the same green as the Re-deal / Restart chrome.
SDL_FColor scrim(float a) { return rgba(0, 0, 0, (int)(150 * a)); }
SDL_FColor panelBody() { return rgba(22, 48, 40); }
SDL_FColor panelEdge() { return rgba(255, 255, 255, 40); }
SDL_FColor tableGreen() { return rgba(11, 84, 62); }
SDL_FColor titleColor() { return rgba(255, 230, 130); }
SDL_FColor bodyColor() { return rgba(230, 234, 232); }
SDL_FColor btnFill(bool on) { return on ? rgba(34, 120, 92) : rgba(34, 120, 92, 110); }
SDL_FColor btnText(bool on) { return on ? rgba(240, 248, 244) : rgba(240, 248, 244, 110); }

struct Page {
    const char* title;
    const char* lines[3];  // trailing entries may be null
    void (*diagram)(Renderer&, SDL_FRect, float);
};

Card card(Suit s, int rank) { return Card{s, rank}; }

// Largest card width for an arrangement `wUnits` card-widths wide and `hUnits`
// card-heights tall, with a little slack so nothing touches the diagram edge.
float fitCardW(SDL_FRect box, float wUnits, float hUnits) {
    return std::min(box.w / wUnits, box.h / (hUnits * kAspect)) * 0.94f;
}

void caption(Renderer& rr, float cx, float y, float px, const char* s) {
    rr.drawText(cx - rr.textWidth(px, s) * 0.5f, y, px, rgba(200, 214, 206), s);
}

// Page 1: a Klondike column (mostly face down) beside a Sawayama one (all face up).
void diagFaceUp(Renderer& rr, SDL_FRect box, float px) {
    const float capH = px * 1.6f;
    SDL_FRect art{box.x, box.y, box.w, box.h - capH};
    const float cw = fitCardW(art, 3.5f, 2.02f), ch = cw * kAspect, fan = ch * 0.34f;
    const float blockH = ch + 3 * fan, gapX = cw * 1.5f;  // wide enough for the captions
    const float x0 = art.x + (art.w - (2 * cw + gapX)) * 0.5f;
    const float x1 = x0 + cw + gapX;
    const float y0 = art.y + (art.h - blockH) * 0.5f;

    for (int i = 0; i < 3; ++i) rr.drawCardBack(SDL_FRect{x0, y0 + i * fan, cw, ch});
    rr.drawCard(SDL_FRect{x0, y0 + 3 * fan, cw, ch}, card(Suit::Spades, 9));

    const Card run[4] = {card(Suit::Spades, 9), card(Suit::Hearts, 8), card(Suit::Clubs, 7),
                         card(Suit::Diamonds, 6)};
    for (int i = 0; i < 4; ++i) rr.drawCard(SDL_FRect{x1, y0 + i * fan, cw, ch}, run[i]);

    const float capY = y0 + blockH + px * 0.35f;
    caption(rr, x0 + cw * 0.5f, capY, px * 0.75f, "Klondike");
    caption(rr, x1 + cw * 0.5f, capY, px * 0.75f, "Sawayama");
}

// Page 2: a low card dropping into an empty column.
void diagEmptyColumn(Renderer& rr, SDL_FRect box, float px) {
    const float capH = px * 1.6f;
    SDL_FRect art{box.x, box.y, box.w, box.h - capH};
    const float cw = fitCardW(art, 2.6f, 1.34f), ch = cw * kAspect;
    const float gapX = cw * 0.6f;
    const float x0 = art.x + (art.w - (2 * cw + gapX)) * 0.5f;
    const float x1 = x0 + cw + gapX;
    const float y = art.y + (art.h - ch * 1.20f) * 0.5f + ch * 0.20f;

    // Left: a King, the only card Klondike would allow. Right: the empty column with
    // a 6 hovering just above it, mid-drop.
    rr.drawCard(SDL_FRect{x0, y, cw, ch}, card(Suit::Clubs, 13));
    rr.drawSlot(SDL_FRect{x1, y, cw, ch});
    rr.drawCard(SDL_FRect{x1, y - ch * 0.20f, cw, ch}, card(Suit::Hearts, 6));

    const float capY = y + ch + px * 0.35f;
    caption(rr, x0 + cw * 0.5f, capY, px * 0.75f, "or a King");
    caption(rr, x1 + cw * 0.5f, capY, px * 0.75f, "any card");
}

// Page 3: the deck and the fanned waste, only the newest card of which is live.
void diagOnePass(Renderer& rr, SDL_FRect box, float px) {
    const float capH = px * 1.6f;
    SDL_FRect art{box.x, box.y, box.w, box.h - capH};
    const float cw = fitCardW(art, 3.5f, 1.32f), ch = cw * kAspect;
    const float gapX = cw * 0.45f;
    const float wasteW = cw + 2 * (cw * 0.54f);
    const float x0 = art.x + (art.w - (cw + gapX + wasteW)) * 0.5f;
    const float y = art.y + (art.h - ch) * 0.5f;

    rr.drawDeck(SDL_FRect{x0, y, cw, ch}, 12);

    const Card w3[3] = {card(Suit::Clubs, 4), card(Suit::Diamonds, 10), card(Suit::Spades, 7)};
    const float wx = x0 + cw + gapX;
    for (int i = 0; i < 3; ++i)
        rr.drawCard(SDL_FRect{wx + i * cw * 0.54f, y, cw, ch}, w3[i], i == 2);

    const float capY = y + ch + px * 0.35f;
    caption(rr, x0 + cw * 0.5f, capY, px * 0.75f, "deck");
    caption(rr, wx + wasteW * 0.5f, capY, px * 0.75f, "top card only");
}

// Page 4: the deck slot before and after it runs dry -- the free cell nobody expects.
// Drawn as a before/after pair so the transformation is the whole picture.
void diagFreeCell(Renderer& rr, SDL_FRect box, float px) {
    const float capH = px * 1.6f;
    SDL_FRect art{box.x, box.y, box.w, box.h - capH};
    const float cw = fitCardW(art, 3.6f, 1.32f), ch = cw * kAspect;
    const float gapX = cw * 0.55f, arrowW = cw * 0.5f;
    const float x0 = art.x + (art.w - (2 * cw + 2 * gapX + arrowW)) * 0.5f;
    const float x1 = x0 + cw + gapX + arrowW + gapX;
    const float y = art.y + (art.h - ch) * 0.5f;

    // Before: the last card coming off the deck.
    rr.drawDeck(SDL_FRect{x0, y, cw, ch}, 1);

    const float apx = px * 1.7f;
    rr.drawText(x0 + cw + gapX + (arrowW - rr.textWidth(apx, ">")) * 0.5f,
                y + (ch - rr.textHeight(apx)) * 0.5f, apx, rgba(255, 230, 130), ">");

    // After: the same slot, now a free cell. The parked card is inset so the cell's
    // brighter ring still frames it -- the ring is the thing being taught.
    rr.drawSlot(SDL_FRect{x1, y, cw, ch}, true);
    const float in = cw * 0.09f;
    rr.drawCard(SDL_FRect{x1 + in, y + in * kAspect, cw - 2 * in, ch - 2 * in * kAspect},
                card(Suit::Spades, 13));

    const float capY = y + ch + px * 0.35f;
    caption(rr, x0 + cw * 0.5f, capY, px * 0.75f, "deck runs out");
    caption(rr, x1 + cw * 0.5f, capY, px * 0.75f, "holds any one card");
}

// Page 4: the auto-mover sending a two up, beside a three that is still holding one.
void diagAutoMove(Renderer& rr, SDL_FRect box, float px) {
    const float capH = px * 1.6f;
    SDL_FRect art{box.x, box.y, box.w, box.h - capH};
    const float cw = fitCardW(art, 3.4f, 2.05f), ch = cw * kAspect;
    const float yTop = art.y + (art.h - ch * 2.05f) * 0.5f;
    const float ax = art.x + art.w * 0.28f - cw * 0.5f;
    const float bx = art.x + art.w * 0.72f - cw * 0.5f;

    // Left: the hearts foundation holding its Ace, with the two on its way up.
    SDL_FRect f{ax, yTop, cw, ch};
    rr.fillRoundedRect(f, f.w * 0.12f, rgba(22, 48, 40));
    rr.drawSuit(Suit::Hearts, f.x + f.w * 0.5f, f.y + f.h * 0.5f, f.h * 0.24f, rgba(79, 99, 91));
    rr.drawCard(f, card(Suit::Hearts, 1));
    rr.drawCard(SDL_FRect{ax, yTop + ch * 1.05f, cw, ch}, card(Suit::Hearts, 2));

    // Right: a black three still doing useful work under a red two.
    rr.drawCard(SDL_FRect{bx, yTop, cw, ch}, card(Suit::Spades, 3));
    rr.drawCard(SDL_FRect{bx, yTop + ch * 0.40f, cw, ch}, card(Suit::Diamonds, 2));

    const float capY = yTop + ch * 2.05f + px * 0.35f;
    caption(rr, ax + cw * 0.5f, capY, px * 0.75f, "2 goes up");
    caption(rr, bx + cw * 0.5f, capY, px * 0.75f, "3 waits for the 2");
}

const Page kPages[] = {
    {"Nothing is hidden",
     {"All 28 tableau cards are dealt face up, so you can plan the whole game.",
      "Build columns down in alternating colors. An ordered run moves as one piece.",
      "Every deal you are given is one that can be won."},
     diagFaceUp},
    {"Any card fills a gap",
     {"Empty a column and any card, or any ordered run, may move into it.",
      "Not just Kings, the way Klondike insists.",
      "Clearing a column early is the strongest move you have."},
     diagEmptyColumn},
    {"One pass through the deck",
     {"Click the deck to turn three cards. There is no second pass and no reset.",
      "Only the newest card of the pile can be played, so spend it carefully."},
     diagOnePass},
    {"The deck slot becomes a free cell",
     {"This is the one Klondike players always miss.",
      "Once the deck is empty its slot turns into a free cell: park any one card there "
      "to dig a column out, and play it back whenever you like.",
      "Emptying the deck costs you nothing but gains you this."},
     diagFreeCell},
    {"Cards go up by themselves",
     {"A card leaves for its foundation on its own, but is held back while it can "
      "still hold a lower card.",
      "Double-click a card to force it up early.",
      "There is no undo. Restart replays this deal; Re-deal starts a new one."},
     diagAutoMove},
};

// A button in the panel's own chrome. Mirrors App::drawButton, but takes an explicit
// text size and can render disabled.
void panelButton(Renderer& rr, SDL_FRect r, const char* label, float px, bool on) {
    rr.fillRoundedRect(r, r.h * 0.25f, btnFill(on));
    float s = px, maxW = r.w * 0.84f, tw = rr.textWidth(s, label);
    if (tw > maxW) {
        s *= maxW / tw;
        tw = maxW;
    }
    rr.drawText(r.x + (r.w - tw) * 0.5f, r.y + (r.h - rr.textHeight(s)) * 0.5f, s, btnText(on),
                label);
}

// Text shrunk to fit a box's width, drawn left-aligned at its cap top.
void fittedText(Renderer& rr, SDL_FRect box, float px, SDL_FColor c, const char* s) {
    float tw = rr.textWidth(px, s);
    if (tw > box.w) px *= box.w / tw;
    rr.drawText(box.x, box.y + (box.h - rr.textHeight(px)) * 0.5f, px, c, s);
}

}  // namespace

int tutorial::pageCount() { return (int)(sizeof kPages / sizeof kPages[0]); }

void tutorial::draw(Renderer& rr, const Layout& L, float outW, float outH, int page,
                    float reveal) {
    page = std::clamp(page, 0, pageCount() - 1);
    const Page& p = kPages[page];

    rr.fillRect(SDL_FRect{0, 0, outW, outH}, scrim(reveal));

    // The card primitives paint opaque colors, so the panel pops in by scaling about
    // its centre rather than fading.
    const float k = 0.94f + 0.06f * reveal;
    const float ccx = L.tutPanel.x + L.tutPanel.w * 0.5f;
    const float ccy = L.tutPanel.y + L.tutPanel.h * 0.5f;
    auto S = [&](SDL_FRect r) {
        return SDL_FRect{ccx + (r.x - ccx) * k, ccy + (r.y - ccy) * k, r.w * k, r.h * k};
    };

    const SDL_FRect panel = S(L.tutPanel);
    const float rad = std::min(panel.w, panel.h) * 0.045f;
    const float edge = std::max(1.5f, rad * 0.10f);
    rr.fillRoundedRect(SDL_FRect{panel.x - edge, panel.y - edge, panel.w + 2 * edge,
                                 panel.h + 2 * edge},
                       rad + edge, panelEdge());
    rr.fillRoundedRect(panel, rad, panelBody());

    const float px = L.tutTextPx * k;
    fittedText(rr, S(L.tutTitle), px * 1.25f, titleColor(), p.title);

    // The diagram sits on the table green: drawSlot punches its centre with exactly
    // that color, so anything else would leave a green hole around empty slots.
    const SDL_FRect diag = S(L.tutDiagram);
    rr.fillRoundedRect(diag, diag.h * 0.06f, tableGreen());
    const float inset = std::min(diag.w, diag.h) * 0.06f;
    p.diagram(rr, SDL_FRect{diag.x + inset, diag.y + inset, diag.w - 2 * inset,
                            diag.h - 2 * inset},
              px);

    // Body copy: wrap at the panel's text size, then shrink once if the block is
    // taller than its box. Shrinking cannot add lines, so one pass always fits.
    const SDL_FRect textBox = S(L.tutText);
    auto blockHeight = [&](float s, std::vector<std::string>* out) {
        if (out) out->clear();
        float h = 0;
        bool first = true;
        for (const char* line : p.lines) {
            if (!line) break;
            if (!first) h += s * 0.45f;
            first = false;
            for (const std::string& w : rr.wrapText(s, textBox.w, line)) {
                if (out) out->push_back(w);
                h += s * 1.35f;
            }
        }
        return h;
    };
    float bpx = px;
    const float need = blockHeight(bpx, nullptr);
    if (need > textBox.h) bpx = std::max(9.0f, bpx * (textBox.h / need));

    float y = textBox.y;
    bool first = true;
    for (const char* line : p.lines) {
        if (!line) break;
        if (!first) y += bpx * 0.45f;
        first = false;
        for (const std::string& w : rr.wrapText(bpx, textBox.w, line)) {
            rr.drawText(textBox.x, y, bpx, bodyColor(), w.c_str());
            y += bpx * 1.35f;
        }
    }

    // Page dots.
    const SDL_FRect dots = S(L.tutDots);
    const int n = pageCount();
    const float d = px * 0.42f, pitch = d * 2.0f;
    float dx = dots.x + (dots.w - (n * d + (n - 1) * (pitch - d))) * 0.5f;
    const float dy = dots.y + (dots.h - d) * 0.5f;
    for (int i = 0; i < n; ++i) {
        rr.fillRoundedRect(SDL_FRect{dx + i * pitch, dy, d, d}, d * 0.5f,
                           i == page ? rgba(240, 248, 244) : rgba(240, 248, 244, 90));
    }

    panelButton(rr, S(L.tutBack), "Back", px, page > 0);
    panelButton(rr, S(L.tutNext), page + 1 < n ? "Next" : "Play", px, true);

    const SDL_FRect close = S(L.tutClose);
    rr.fillRoundedRect(close, close.h * 0.25f, rgba(255, 255, 255, 28));
    const float cs = px * 1.05f;
    rr.drawText(close.x + (close.w - rr.textWidth(cs, "X")) * 0.5f,
                close.y + (close.h - rr.textHeight(cs)) * 0.5f, cs, bodyColor(), "X");
}
