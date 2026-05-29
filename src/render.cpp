#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kCardAspect = 1.42f;     // height / width
constexpr float kDebugGlyph = 8.0f;      // SDL debug font cell size, px

SDL_FColor red() { return rgba(196, 30, 48); }
SDL_FColor black() { return rgba(28, 28, 38); }
}  // namespace

const char* rankString(int rank) {
    static const char* names[] = {"",  "A", "2", "3", "4",  "5", "6",
                                  "7", "8", "9", "10", "J", "Q", "K"};
    return (rank >= 1 && rank <= 13) ? names[rank] : "?";
}

SDL_FRect tableauCardRect(const Layout& L, int col, int i) {
    SDL_FRect rc = L.tableau[col];
    rc.y += i * L.fanY;
    return rc;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
// Fan offset between stacked waste cards: comfortable when few, shrinking to
// fit within `avail` so the entire waste stays visible no matter how many.
static float wasteFanFor(float cardW, float avail, int count) {
    float comfy = cardW * 0.34f;
    if (count <= 1) return comfy;
    return std::min(comfy, (avail - cardW) / (count - 1));
}

Layout computeLayout(float w, float h, int wasteCount) {
    Layout L;
    L.vertical = h > w;
    const float margin = std::max(8.0f, std::min(w, h) * 0.02f);

    // A tableau fan offset of at least ~0.30*cardH keeps each buried card's
    // corner index readable; cap it so short stacks don't spread too far.
    auto fanForHeight = [](float cardH, float availH) {
        return std::clamp((availH - cardH) / 19.0f, cardH * 0.30f, cardH * 0.42f);
    };

    if (L.vertical) {
        const float gap = std::max(4.0f, w * 0.012f);
        L.cardW = (w - 2 * margin - 6 * gap) / 7.0f;
        L.cardH = L.cardW * kCardAspect;
        L.uiTextScale = std::max(1.5f, L.cardH * 0.040f);

        const float winsH = kDebugGlyph * L.uiTextScale;
        const float btnH = std::max(22.0f, L.cardH * 0.34f);
        const float headerH = winsH + gap + btnH + gap;

        L.winsAnchor = SDL_FRect{w - margin, margin, 0, 0};
        const float btnTop = margin + winsH + gap;
        const float redealW = L.cardW * 1.6f;
        L.redealBtn = SDL_FRect{w - margin - redealW, btnTop, redealW, btnH};
        L.muteBtn = SDL_FRect{L.redealBtn.x - gap - btnH, btnTop, btnH, btnH};

        // Row A: stock (left) + foundations (right), arranged horizontally on top.
        const float rowA = margin + headerH;
        L.stock = SDL_FRect{margin, rowA, L.cardW, L.cardH};
        for (int i = 0; i < 4; ++i) {
            float x = (w - margin - L.cardW) - (3 - i) * (L.cardW + gap);
            L.foundations[i] = SDL_FRect{x, rowA, L.cardW, L.cardH};
        }

        // Row B: the waste gets its own full-width row to spill across.
        const float rowB = rowA + L.cardH + gap;
        L.waste = SDL_FRect{margin, rowB, L.cardW, L.cardH};
        L.wasteFan = wasteFanFor(L.cardW, w - 2 * margin, wasteCount);

        const float tabTop = rowB + L.cardH + gap * 1.2f;
        for (int col = 0; col < 7; ++col)
            L.tableau[col] = SDL_FRect{margin + col * (L.cardW + gap), tabTop, L.cardW, L.cardH};
        L.fanY = fanForHeight(L.cardH, h - tabTop - margin);
    } else {
        // Horizontal: foundations stacked on the left. Cards are sized smaller so
        // tableau fans stay readable, and the waste spills along the top row.
        const float widthBound = (w - 3 * margin) / 10.5f;
        const float colFanBound = (h - 2 * margin) / 7.8f;  // room for a ~12-card fan
        L.cardW = std::min(widthBound, colFanBound);
        L.cardH = L.cardW * kCardAspect;
        L.uiTextScale = std::max(1.5f, L.cardH * 0.040f);

        const float fgap = L.cardH * 0.12f;
        const float colGap = L.cardW * 0.35f;  // breathing room between columns
        const float bigPad = L.cardW * 0.45f;  // gap between foundations and tableau

        // Centre the 4-foundation stack vertically on the left.
        const float fStackH = 4 * L.cardH + 3 * fgap;
        const float fTop = std::max(margin, (h - fStackH) * 0.5f);
        for (int i = 0; i < 4; ++i)
            L.foundations[i] = SDL_FRect{margin, fTop + i * (L.cardH + fgap), L.cardW, L.cardH};

        // Wins counter + buttons cluster, top-right.
        const float winsH = kDebugGlyph * L.uiTextScale;
        const float btnH = std::max(22.0f, L.cardH * 0.30f);
        const float redealW = L.cardW * 1.6f;
        L.winsAnchor = SDL_FRect{w - margin, margin, 0, 0};
        const float btnTop = margin + winsH + margin * 0.6f;
        L.redealBtn = SDL_FRect{w - margin - redealW, btnTop, redealW, btnH};
        L.muteBtn = SDL_FRect{L.redealBtn.x - margin * 0.6f - btnH, btnTop, btnH, btnH};
        const float clusterW = std::max(redealW + margin + btnH, 10 * kDebugGlyph * L.uiTextScale);
        const float clusterLeft = w - margin - clusterW;

        const float rightX = margin + L.cardW + bigPad;
        L.stock = SDL_FRect{rightX, margin, L.cardW, L.cardH};
        L.waste = SDL_FRect{rightX + L.cardW + colGap, margin, L.cardW, L.cardH};
        L.wasteFan = wasteFanFor(L.cardW, (clusterLeft - margin) - L.waste.x, wasteCount);

        const float tabTop = margin + L.cardH + L.cardH * 0.18f;
        for (int col = 0; col < 7; ++col)
            L.tableau[col] = SDL_FRect{rightX + col * (L.cardW + colGap), tabTop, L.cardW, L.cardH};
        L.fanY = fanForHeight(L.cardH, h - tabTop - margin);
    }
    return L;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
bool Renderer::init(SDL_Renderer* r) {
    r_ = r;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    return true;
}

void Renderer::clear(SDL_FColor c) {
    SDL_SetRenderDrawColorFloat(r_, c.r, c.g, c.b, c.a);
    SDL_RenderClear(r_);
}

void Renderer::fillRect(SDL_FRect rc, SDL_FColor c) {
    SDL_SetRenderDrawColorFloat(r_, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r_, &rc);
}

void Renderer::fillConvex(const SDL_FPoint* pts, int n, SDL_FColor c) {
    if (n < 3) return;
    SDL_FPoint center{0, 0};
    for (int i = 0; i < n; ++i) {
        center.x += pts[i].x;
        center.y += pts[i].y;
    }
    center.x /= n;
    center.y /= n;

    std::vector<SDL_Vertex> v;
    v.reserve(n + 1);
    v.push_back(SDL_Vertex{center, c, {0, 0}});
    for (int i = 0; i < n; ++i) v.push_back(SDL_Vertex{pts[i], c, {0, 0}});

    std::vector<int> idx;
    idx.reserve(n * 3);
    for (int i = 0; i < n; ++i) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(1 + (i + 1) % n);
    }
    SDL_RenderGeometry(r_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Renderer::drawCircle(float cx, float cy, float radius, SDL_FColor c) {
    constexpr int kN = 18;
    SDL_FPoint pts[kN];
    for (int i = 0; i < kN; ++i) {
        float a = 2 * kPi * i / kN;
        pts[i] = SDL_FPoint{cx + radius * std::cos(a), cy + radius * std::sin(a)};
    }
    fillConvex(pts, kN, c);
}

void Renderer::fillRoundedRect(SDL_FRect rc, float radius, SDL_FColor c) {
    radius = std::min(radius, std::min(rc.w, rc.h) * 0.5f);
    constexpr int kSeg = 5;
    std::vector<SDL_FPoint> pts;
    pts.reserve((kSeg + 1) * 4);

    struct Corner {
        float cx, cy, a0;
    };
    const Corner corners[4] = {
        {rc.x + radius, rc.y + radius, 180.0f},               // top-left
        {rc.x + rc.w - radius, rc.y + radius, 270.0f},        // top-right
        {rc.x + rc.w - radius, rc.y + rc.h - radius, 0.0f},   // bottom-right
        {rc.x + radius, rc.y + rc.h - radius, 90.0f},         // bottom-left
    };
    for (const auto& cn : corners) {
        for (int s = 0; s <= kSeg; ++s) {
            float a = (cn.a0 + 90.0f * s / kSeg) * kPi / 180.0f;
            pts.push_back(SDL_FPoint{cn.cx + radius * std::cos(a), cn.cy + radius * std::sin(a)});
        }
    }
    fillConvex(pts.data(), (int)pts.size(), c);
}

void Renderer::drawSuit(Suit s, float cx, float cy, float size) {
    const SDL_FColor c = isRed(s) ? red() : black();
    auto tri = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
        SDL_FPoint p[3] = {{cx + x0 * size, cy + y0 * size},
                           {cx + x1 * size, cy + y1 * size},
                           {cx + x2 * size, cy + y2 * size}};
        fillConvex(p, 3, c);
    };
    auto circ = [&](float x, float y, float r) { drawCircle(cx + x * size, cy + y * size, r * size, c); };

    switch (s) {
        case Suit::Diamonds: {
            SDL_FPoint p[4] = {{cx, cy - 0.50f * size},
                               {cx + 0.37f * size, cy},
                               {cx, cy + 0.50f * size},
                               {cx - 0.37f * size, cy}};
            fillConvex(p, 4, c);
            break;
        }
        case Suit::Hearts:
            circ(-0.20f, -0.12f, 0.26f);
            circ(0.20f, -0.12f, 0.26f);
            tri(-0.45f, -0.04f, 0.45f, -0.04f, 0.0f, 0.52f);
            break;
        case Suit::Spades:
            tri(0.0f, -0.52f, -0.46f, 0.06f, 0.46f, 0.06f);
            circ(-0.23f, 0.12f, 0.25f);
            circ(0.23f, 0.12f, 0.25f);
            tri(-0.17f, 0.50f, 0.17f, 0.50f, 0.0f, 0.06f);
            break;
        case Suit::Clubs:
            circ(0.0f, -0.24f, 0.24f);
            circ(-0.25f, 0.10f, 0.24f);
            circ(0.25f, 0.10f, 0.24f);
            tri(-0.16f, 0.52f, 0.16f, 0.52f, 0.0f, 0.04f);
            break;
    }
}

void Renderer::drawCard(SDL_FRect rc, Card card, bool highlight) {
    const float radius = rc.w * 0.12f;
    const float border = std::max(1.5f, rc.w * 0.03f);
    fillRoundedRect(rc, radius, highlight ? rgba(255, 236, 150) : rgba(20, 20, 30));
    SDL_FRect face{rc.x + border, rc.y + border, rc.w - 2 * border, rc.h - 2 * border};
    fillRoundedRect(face, radius - border, rgba(248, 246, 240));

    const SDL_FColor c = isRed(card.suit) ? red() : black();
    const float scale = std::max(1.0f, rc.h * 0.028f);
    const char* rs = rankString(card.rank);

    // Top-left corner: rank over a small pip.
    float pad = rc.w * 0.10f;
    drawText(rc.x + pad, rc.y + pad, scale, c, rs);
    drawSuit(card.suit, rc.x + pad + kDebugGlyph * scale * 0.5f,
             rc.y + pad + kDebugGlyph * scale + rc.h * 0.07f, rc.h * 0.11f);

    // Large central pip.
    drawSuit(card.suit, rc.x + rc.w * 0.5f, rc.y + rc.h * 0.55f, rc.h * 0.34f);
}

void Renderer::drawCardBack(SDL_FRect rc) {
    const float radius = rc.w * 0.12f;
    const float border = std::max(1.5f, rc.w * 0.03f);
    fillRoundedRect(rc, radius, rgba(20, 20, 30));
    SDL_FRect a{rc.x + border, rc.y + border, rc.w - 2 * border, rc.h - 2 * border};
    fillRoundedRect(a, radius - border, rgba(46, 78, 150));
    SDL_FRect b{rc.x + rc.w * 0.16f, rc.y + rc.h * 0.12f, rc.w * 0.68f, rc.h * 0.76f};
    fillRoundedRect(b, radius * 0.6f, rgba(70, 110, 196));
}

void Renderer::drawDeck(SDL_FRect rc, int cardsLeft) {
    if (cardsLeft <= 0) {
        drawSlot(rc);
        return;
    }
    // Each click draws three, so the visible thickness tracks draws remaining.
    int draws = (cardsLeft + 2) / 3;
    int layers = std::min(draws, 8);
    const float off = std::max(1.0f, rc.w * 0.028f);
    const float radius = rc.w * 0.12f;
    const float border = std::max(1.5f, rc.w * 0.03f);
    // Edges of the cards beneath the top, offset down-right, drawn back-to-front.
    for (int i = layers - 1; i >= 1; --i) {
        SDL_FRect e{rc.x + i * off, rc.y + i * off, rc.w, rc.h};
        fillRoundedRect(e, radius, rgba(18, 18, 28));
        SDL_FRect inner{e.x + border, e.y + border, e.w - 2 * border, e.h - 2 * border};
        fillRoundedRect(inner, radius - border, rgba(38, 64, 122));
    }
    drawCardBack(rc);
}

void Renderer::drawSlot(SDL_FRect rc, bool freecell) {
    const float radius = rc.w * 0.12f;
    const float border = std::max(1.5f, rc.w * 0.025f);
    fillRoundedRect(rc, radius, rgba(255, 255, 255, freecell ? 60 : 36));
    SDL_FRect inner{rc.x + border, rc.y + border, rc.w - 2 * border, rc.h - 2 * border};
    // Punch out the centre with the table colour to leave a thin ring.
    fillRoundedRect(inner, radius - border, rgba(11, 84, 62));
}

void Renderer::drawText(float x, float y, float scale, SDL_FColor c, const char* str) {
    SDL_SetRenderDrawColorFloat(r_, c.r, c.g, c.b, c.a);
    float sx, sy;
    SDL_GetRenderScale(r_, &sx, &sy);
    SDL_SetRenderScale(r_, scale, scale);
    SDL_RenderDebugText(r_, x / scale, y / scale, str);
    SDL_SetRenderScale(r_, sx, sy);
}

float Renderer::textWidth(float scale, const char* str) const {
    return std::strlen(str) * kDebugGlyph * scale;
}

float Renderer::textHeight(float scale) const { return kDebugGlyph * scale; }

void Renderer::drawSpeaker(SDL_FRect rc, bool muted, SDL_FColor c) {
    // Speaker body (small rect) + cone (triangle) centred in the button.
    float cx = rc.x + rc.w * 0.40f, cy = rc.y + rc.h * 0.5f;
    float s = std::min(rc.w, rc.h) * 0.5f;
    fillRect(SDL_FRect{cx - s * 0.55f, cy - s * 0.22f, s * 0.30f, s * 0.44f}, c);
    SDL_FPoint cone[3] = {{cx - s * 0.25f, cy - s * 0.20f},
                          {cx - s * 0.25f, cy + s * 0.20f},
                          {cx + s * 0.15f, cy + s * 0.45f}};
    SDL_FPoint cone2[3] = {{cx - s * 0.25f, cy - s * 0.20f},
                           {cx + s * 0.15f, cy - s * 0.45f},
                           {cx + s * 0.15f, cy + s * 0.45f}};
    fillConvex(cone, 3, c);
    fillConvex(cone2, 3, c);
    SDL_SetRenderDrawColorFloat(r_, c.r, c.g, c.b, c.a);
    if (muted) {
        float x0 = cx + s * 0.30f, x1 = cx + s * 0.75f, y0 = cy - s * 0.30f, y1 = cy + s * 0.30f;
        SDL_RenderLine(r_, x0, y0, x1, y1);
        SDL_RenderLine(r_, x0, y1, x1, y0);
    } else {
        // Two sound arcs approximated with short line segments.
        for (int k = 1; k <= 2; ++k) {
            float rr = s * (0.30f + 0.22f * k);
            float bx = cx + s * 0.20f;
            float prevx = 0, prevy = 0;
            for (int i = 0; i <= 8; ++i) {
                float a = -0.6f + 1.2f * i / 8.0f;
                float px = bx + rr * std::cos(a);
                float py = cy + rr * std::sin(a);
                if (i) SDL_RenderLine(r_, prevx, prevy, px, py);
                prevx = px;
                prevy = py;
            }
        }
    }
}
