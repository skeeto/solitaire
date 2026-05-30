#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "font_data.h"     // embedded Inter (Regular) subset
#include "stb_truetype.h"  // declarations; implementation lives in stb_impl.cpp

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kCardAspect = 1.42f;  // height / width

SDL_FColor red() { return rgba(196, 30, 48); }
SDL_FColor black() { return rgba(28, 28, 38); }
}  // namespace

// Antialiased TrueType text: the embedded font is baked once into a glyph atlas
// texture; strings draw as textured quads tinted to the requested color and
// scaled (with linear filtering) so they stay smooth at any size.
class GlyphFont {
public:
    bool init(SDL_Renderer* r) {
        r_ = r;
        atlasW_ = atlasH_ = 1024;
        std::vector<unsigned char> alpha((size_t)atlasW_ * atlasH_, 0);
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), atlasW_, atlasH_, 0, 1, nullptr)) return false;
        stbtt_PackSetOversampling(&pc, 2, 2);
        stbtt_PackFontRange(&pc, kFontTTF, 0, bakePx_, 32, 95, packed_);
        stbtt_PackEnd(&pc);

        stbtt_fontinfo info;
        stbtt_InitFont(&info, kFontTTF, stbtt_GetFontOffsetForIndex(kFontTTF, 0));
        float sc = stbtt_ScaleForPixelHeight(&info, bakePx_);
        // Use cap height (top of 'H' above the baseline) as the text height so
        // (x, y) is the cap top and short labels center cleanly.
        int hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
        if (stbtt_GetCodepointBox(&info, 'H', &hx0, &hy0, &hx1, &hy1) && hy1 > 0) {
            capBaked_ = hy1 * sc;
        } else {
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
            capBaked_ = 0.72f * asc * sc;
        }

        std::vector<unsigned char> rgba((size_t)atlasW_ * atlasH_ * 4);
        for (size_t i = 0; i < (size_t)atlasW_ * atlasH_; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = alpha[i];
        }
        atlas_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, atlasW_, atlasH_);
        if (!atlas_) return false;
        SDL_UpdateTexture(atlas_, nullptr, rgba.data(), atlasW_ * 4);
        SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_LINEAR);
        return true;
    }

    void destroy() {
        if (atlas_) SDL_DestroyTexture(atlas_);
        atlas_ = nullptr;
    }

    float width(float px, const char* s) const {
        float w = 0;
        for (; *s; ++s) {
            int c = (unsigned char)*s;
            if (c < 32 || c >= 127) c = 32;
            w += packed_[c - 32].xadvance;
        }
        return w * (px / bakePx_);
    }

    float height(float px) const { return capBaked_ * (px / bakePx_); }

    void draw(float x, float y, float px, SDL_FColor col, const char* s) {
        if (!atlas_) return;
        float scale = px / bakePx_;
        SDL_SetTextureColorModFloat(atlas_, col.r, col.g, col.b);
        SDL_SetTextureAlphaModFloat(atlas_, col.a);
        float cx = 0, cy = capBaked_;  // baseline so the cap top lands at y
        for (; *s; ++s) {
            int c = (unsigned char)*s;
            if (c < 32 || c >= 127) c = 32;
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(packed_, atlasW_, atlasH_, c - 32, &cx, &cy, &q, 0);
            SDL_FRect src{q.s0 * atlasW_, q.t0 * atlasH_, (q.s1 - q.s0) * atlasW_, (q.t1 - q.t0) * atlasH_};
            SDL_FRect dst{x + q.x0 * scale, y + q.y0 * scale, (q.x1 - q.x0) * scale, (q.y1 - q.y0) * scale};
            SDL_RenderTexture(r_, atlas_, &src, &dst);
        }
    }

private:
    SDL_Renderer* r_ = nullptr;
    SDL_Texture* atlas_ = nullptr;
    float bakePx_ = 48.0f;
    float capBaked_ = 0;
    int atlasW_ = 0, atlasH_ = 0;
    stbtt_packedchar packed_[95];
};

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

SDL_FRect wasteCardRect(const Layout& L, int k) {
    int per = std::max(1, L.wastePerRow);
    int row = k / per, col = k % per;
    return SDL_FRect{L.waste.x + col * L.wasteFan, L.waste.y + row * L.wasteRowStep, L.cardW, L.cardH};
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
// Lay out the waste: fan rightward at a readable offset, wrapping to a second
// row (using vertical space) so cards never cram together. Fills wasteFan,
// wastePerRow and wasteRowStep on the layout.
static constexpr int kWasteMaxRows = 2;
static constexpr float kWasteBand = 1.0f + (kWasteMaxRows - 1) * 0.52f;  // cardH multiples

static void computeWaste(Layout& L, float availWasteW, int count) {
    const float comfy = L.cardW * 0.54f;  // wide enough to reveal a two-digit rank + suit
    L.wasteRowStep = L.cardH * 0.52f;
    int perRow = std::max(1, (int)std::floor((availWasteW - L.cardW) / comfy) + 1);
    int rows = (count <= 0) ? 1 : (count + perRow - 1) / perRow;
    if (rows > kWasteMaxRows) {
        perRow = (count + kWasteMaxRows - 1) / kWasteMaxRows;  // pack into the row limit
        L.wasteFan = std::max(L.cardW * 0.16f, (availWasteW - L.cardW) / std::max(1, perRow - 1));
    } else {
        L.wasteFan = comfy;
    }
    L.wastePerRow = std::max(1, perRow);
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
        L.uiTextPx = std::max(12.0f, L.cardH * 0.32f);

        const float winsH = L.uiTextPx;
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

        // Row B: the waste gets its own full-width band to spill/wrap across,
        // placed below the stock's rendered thickness so it never overlaps.
        const float deckExtent = 8.0f * std::max(1.0f, L.cardW * 0.028f);
        const float rowB = rowA + L.cardH + deckExtent + gap;
        L.waste = SDL_FRect{margin, rowB, L.cardW, L.cardH};
        computeWaste(L, w - 2 * margin, wasteCount);

        const float tabTop = rowB + L.cardH * kWasteBand + gap * 1.2f;
        for (int col = 0; col < 7; ++col)
            L.tableau[col] = SDL_FRect{margin + col * (L.cardW + gap), tabTop, L.cardW, L.cardH};
        L.fanY = fanForHeight(L.cardH, h - tabTop - margin);
    } else {
        // Horizontal: foundations stacked on the left. Cards are sized smaller so
        // tableau fans stay readable, and the waste spills along the top row.
        const float widthBound = (w - 3 * margin) / 10.5f;
        const float colFanBound = (h - 2 * margin) / 8.3f;  // room for a ~12-card fan + waste band
        L.cardW = std::min(widthBound, colFanBound);
        L.cardH = L.cardW * kCardAspect;
        L.uiTextPx = std::max(12.0f, L.cardH * 0.32f);

        const float fgap = L.cardH * 0.12f;
        const float colGap = L.cardW * 0.35f;  // breathing room between columns
        const float bigPad = L.cardW * 0.45f;  // gap between foundations and tableau

        // Centre the 4-foundation stack vertically on the left.
        const float fStackH = 4 * L.cardH + 3 * fgap;
        const float fTop = std::max(margin, (h - fStackH) * 0.5f);
        for (int i = 0; i < 4; ++i)
            L.foundations[i] = SDL_FRect{margin, fTop + i * (L.cardH + fgap), L.cardW, L.cardH};

        // Wins counter + buttons cluster, top-right.
        const float winsH = L.uiTextPx;
        const float btnH = std::max(22.0f, L.cardH * 0.30f);
        const float redealW = L.cardW * 1.6f;
        L.winsAnchor = SDL_FRect{w - margin, margin, 0, 0};
        const float btnTop = margin + winsH + margin * 0.6f;
        L.redealBtn = SDL_FRect{w - margin - redealW, btnTop, redealW, btnH};
        L.muteBtn = SDL_FRect{L.redealBtn.x - margin * 0.6f - btnH, btnTop, btnH, btnH};
        // Reserve room for the wins text (~10 chars) and the button row.
        const float clusterW = std::max(redealW + margin + btnH, 10.0f * L.uiTextPx * 0.62f);
        const float clusterLeft = w - margin - clusterW;

        const float rightX = margin + L.cardW + bigPad;
        L.stock = SDL_FRect{rightX, margin, L.cardW, L.cardH};
        L.waste = SDL_FRect{rightX + L.cardW + colGap, margin, L.cardW, L.cardH};
        computeWaste(L, (clusterLeft - margin) - L.waste.x, wasteCount);

        const float tabTop = margin + L.cardH * kWasteBand + L.cardH * 0.10f;
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
    font_ = new GlyphFont();
    if (!font_->init(r_)) {
        SDL_Log("font: failed to build glyph atlas");
        delete font_;
        font_ = nullptr;
    }
    return true;
}

void Renderer::shutdown() {
    if (font_) {
        font_->destroy();
        delete font_;
        font_ = nullptr;
    }
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

    // Antialiasing: a ~1px fringe of vertices offset radially outward at alpha 0,
    // so the GPU interpolates a smooth edge. Where this fringe overlaps another
    // shape of the same opaque color it resolves to that color (no seams).
    constexpr float kAA = 1.2f;
    SDL_FColor edge = c;
    edge.a = 0.0f;

    std::vector<SDL_Vertex> v;
    v.reserve(2 * n + 1);
    v.push_back(SDL_Vertex{center, c, {0, 0}});          // 0: center
    for (int i = 0; i < n; ++i)                          // 1..n: inner perimeter (solid)
        v.push_back(SDL_Vertex{pts[i], c, {0, 0}});
    for (int i = 0; i < n; ++i) {                        // n+1..2n: outer fringe (transparent)
        float dx = pts[i].x - center.x, dy = pts[i].y - center.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) len = 1.0f;
        SDL_FPoint o{pts[i].x + dx / len * kAA, pts[i].y + dy / len * kAA};
        v.push_back(SDL_Vertex{o, edge, {0, 0}});
    }

    std::vector<int> idx;
    idx.reserve(n * 9);
    const int inner = 1, outer = 1 + n;
    for (int i = 0; i < n; ++i) {
        int a = inner + i, b = inner + (i + 1) % n;
        int oa = outer + i, ob = outer + (i + 1) % n;
        idx.push_back(0);  idx.push_back(a);  idx.push_back(b);   // interior fan
        idx.push_back(a);  idx.push_back(b);  idx.push_back(ob);  // fringe quad
        idx.push_back(a);  idx.push_back(ob); idx.push_back(oa);
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

// Classic parametric heart curve, sampled once into a single closed outline and
// normalized to a unit (height = 1) box centred on its bounding box. Drawing it
// as one polygon avoids the tangent/seam artifacts of a circles-plus-triangle
// construction; the spade reuses it vertically flipped.
static const std::vector<SDL_FPoint>& heartUnit() {
    static const std::vector<SDL_FPoint> pts = [] {
        const int N = 72;
        std::vector<SDL_FPoint> raw(N);
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (int i = 0; i < N; ++i) {
            float t = 2.0f * kPi * i / N;
            float x = 16.0f * std::pow(std::sin(t), 3.0f);
            float y = -(13.0f * std::cos(t) - 5.0f * std::cos(2 * t) - 2.0f * std::cos(3 * t) -
                        std::cos(4 * t));  // negate: point sits at the bottom in screen space
            raw[i] = {x, y};
            minx = std::min(minx, x);
            maxx = std::max(maxx, x);
            miny = std::min(miny, y);
            maxy = std::max(maxy, y);
        }
        float ccx = (minx + maxx) * 0.5f, ccy = (miny + maxy) * 0.5f, sc = 1.0f / (maxy - miny);
        std::vector<SDL_FPoint> p(N);
        for (int i = 0; i < N; ++i) p[i] = {(raw[i].x - ccx) * sc, (raw[i].y - ccy) * sc};
        return p;
    }();
    return pts;
}

void Renderer::drawSuit(Suit s, float cx, float cy, float size) {
    drawSuit(s, cx, cy, size, isRed(s) ? red() : black());
}

void Renderer::drawSuit(Suit s, float cx, float cy, float size, SDL_FColor c) {
    auto tri = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
        SDL_FPoint p[3] = {{cx + x0 * size, cy + y0 * size},
                           {cx + x1 * size, cy + y1 * size},
                           {cx + x2 * size, cy + y2 * size}};
        fillConvex(p, 3, c);
    };
    auto circ = [&](float x, float y, float r) { drawCircle(cx + x * size, cy + y * size, r * size, c); };
    // Heart outline scaled to `size`; flipY = -1 draws the spade body (inverted).
    auto heart = [&](float flipY) {
        const auto& u = heartUnit();
        std::vector<SDL_FPoint> p(u.size());
        for (size_t i = 0; i < u.size(); ++i)
            p[i] = {cx + u[i].x * size, cy + u[i].y * size * flipY};
        fillConvex(p.data(), (int)p.size(), c);
    };

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
            heart(1.0f);
            break;
        case Suit::Spades:
            heart(-1.0f);                                   // inverted-heart body
            tri(0.0f, 0.06f, -0.16f, 0.62f, 0.16f, 0.62f);  // long club-style stem
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
    // Bold, generously sized corner index so it stays readable on small screens
    // and through a fanned overlap.
    const float px = rc.h * 0.20f;
    const char* rs = rankString(card.rank);

    // Top-left corner: rank over a small pip centred under it. Match the top
    // inset to the left inset so the rank sits balanced in the corner.
    float pad = rc.w * 0.08f;
    float topY = rc.y + pad;
    drawText(rc.x + pad, topY, px, c, rs);
    drawSuit(card.suit, rc.x + pad + textWidth(px, rs) * 0.5f, topY + px + rc.h * 0.02f, rc.h * 0.10f);

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

void Renderer::drawText(float x, float y, float px, SDL_FColor c, const char* str) {
    if (font_) font_->draw(x, y, px, c, str);
}

float Renderer::textWidth(float px, const char* str) const {
    return font_ ? font_->width(px, str) : 0.0f;
}

float Renderer::textHeight(float px) const { return font_ ? font_->height(px) : px; }

void Renderer::drawSpeaker(SDL_FRect rc, bool muted, SDL_FColor c) {
    // Speaker body (small rect) + cone (triangle) centred in the button.
    float cx = rc.x + rc.w * 0.40f, cy = rc.y + rc.h * 0.5f;
    float s = std::min(rc.w, rc.h) * 0.5f;
    float th = std::max(2.2f, s * 0.16f);  // stroke thickness for waves / mute X

    // A thick, antialiased line segment drawn as a quad.
    auto thickLine = [&](float x0, float y0, float x1, float y1) {
        float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) return;
        float nx = -dy / len * th * 0.5f, ny = dx / len * th * 0.5f;
        SDL_FPoint p[4] = {{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}};
        fillConvex(p, 4, c);
    };

    fillRect(SDL_FRect{cx - s * 0.55f, cy - s * 0.22f, s * 0.30f, s * 0.44f}, c);
    SDL_FPoint cone[3] = {{cx - s * 0.25f, cy - s * 0.20f},
                          {cx - s * 0.25f, cy + s * 0.20f},
                          {cx + s * 0.15f, cy + s * 0.45f}};
    SDL_FPoint cone2[3] = {{cx - s * 0.25f, cy - s * 0.20f},
                           {cx + s * 0.15f, cy - s * 0.45f},
                           {cx + s * 0.15f, cy + s * 0.45f}};
    fillConvex(cone, 3, c);
    fillConvex(cone2, 3, c);

    if (muted) {
        float x0 = cx + s * 0.28f, x1 = cx + s * 0.82f, y0 = cy - s * 0.32f, y1 = cy + s * 0.32f;
        thickLine(x0, y0, x1, y1);
        thickLine(x0, y1, x1, y0);
    } else {
        // Two thick sound arcs built from short thick segments.
        for (int k = 1; k <= 2; ++k) {
            float rr = s * (0.30f + 0.26f * k);
            float bx = cx + s * 0.18f;
            float prevx = 0, prevy = 0;
            for (int i = 0; i <= 8; ++i) {
                float a = -0.62f + 1.24f * i / 8.0f;
                float px = bx + rr * std::cos(a);
                float py = cy + rr * std::sin(a);
                if (i) thickLine(prevx, prevy, px, py);
                prevx = px;
                prevy = py;
            }
        }
    }
}
