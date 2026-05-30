# Sawayama Solitaire

A clone of Zachtronics' **Sawayama Solitaire** (the single-pass Klondike variant
from *Last Call BBS*), built in C++20 with SDL3. It runs natively on desktop and
on the web as an installable PWA. Rendering is procedural (no image assets) and
sound effects are synthesized at runtime, so SDL3 is the only third-party
library; text uses the bundled single-header [`stb_truetype`](third_party/stb_truetype.h)
with a small embedded **Inter** subset (SIL OFL 1.1 — see [assets/](assets/README.md)).

See [docs/sawayama-solitaire.md](docs/sawayama-solitaire.md) for the full rules.

## How to play

- All tableau cards are face up. Build columns **down in alternating colors**.
- Drag a card or an ordered run onto another column; **any** card or run may go
  into an empty column.
- Click the stock to **draw three** at a time — there is only **one pass**, no
  reset. When the stock runs out, that slot becomes a single **free cell**.
- Cards advance to the foundations automatically (a conservative auto-mover);
  **double-click** a card to force it up.
- **RE-DEAL** starts a new game at any time. The speaker icon toggles sound.
  Lifetime wins are shown top-right and persisted.

## Build — native

Requires CMake ≥ 3.24 and a C++20 compiler. SDL3 is fetched automatically over
HTTPS (pinned by SHA256) and linked statically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/solitaire
```

## Build — web (Emscripten)

Requires the Emscripten SDK (`emcc` on PATH).

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
# serve the output (a service worker requires http://, not file://)
python3 -m http.server -d build-web 8000
# open http://localhost:8000/index.html
```

The build emits `index.html` / `index.js` / `index.wasm` plus the PWA support
files (`manifest.webmanifest`, `sw.js`, icons), so the page is installable and
works offline after the first load.
