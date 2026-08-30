// Paged first-run tutorial: the page copy plus procedurally drawn card diagrams,
// composed from the render.cpp primitives. Like render.cpp this file is free of
// interaction state -- the page index and the open/closed flag live in main.cpp's
// App, which hit-tests the L.tut* rects directly.
#pragma once

#include <SDL3/SDL.h>

#include "render.hpp"

namespace tutorial {

int pageCount();

// Draw the scrim and the panel for `page` (clamped). outW/outH size the scrim so it
// covers the safe-area margins too, like the win overlay. `reveal` in [0,1] fades the
// scrim and scales the panel about its centre (1 = fully open).
void draw(Renderer& rr, const Layout& L, float outW, float outH, int page, float reveal);

}  // namespace tutorial
