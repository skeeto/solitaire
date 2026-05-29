# Sawayama Solitaire (Last Call BBS): Complete Rules, Mechanics, and UI Documentation

## TL;DR
- **Sawayama Solitaire is a re-imagined, single-pass Klondike** played with a standard 52-card deck dealt into seven fully face-up tableau columns; you win by building all four suit foundations from Ace to King, and its three defining twists are an all-face-up tableau, a stock you may pass through only once (drawing three at a time), and the ability to drop any card — not just a King — into an emptied column.
- **The biggest mechanical departures from Klondike** are: any card or ordered stack can fill an empty column; once the stock is exhausted its slot permanently becomes a single free cell; there is no redeal and no undo; and cards advance to the foundations automatically under a deliberately "conservative" auto-mover that "never causes you to lose a game" (you can also force a card up with a double-click).
- **Presentation is pure Zachtronics retro-computing**: the game is pre-installed on the fictional Sawayama Z5 Powerlance, rendered in high-resolution pixel art with a CRT-style warped look and an FM-synth soundtrack, with a wins counter, a triangle-shaped rules button, and no undo button — choices that make it noticeably more strategic and less forgiving than classic Windows solitaire.

## Key Findings
- Sawayama Solitaire is one of eight games in Zachtronics' final release, **Last Call BBS** (2022), and was later included in **The Zachtronics Solitaire Collection** (2022). It is the only game pre-installed on the in-fiction computer.
- It descends from the Russian-language minigame "ПАСЬЯНС" ("Pasyans"/"Patience") in EXAPUNKS (2018), but that ancestor used a 36-card deck and a different goal, so the two are related but mechanically distinct.
- The Last Call BBS version uses a full standard 52-card, four-suit deck and the Klondike objective: build four foundations, one per suit, ascending Ace→King.
- Tableau building is descending in alternating colors; full ordered sequences of any length can be moved together.
- The single-pass stock (draw-three, no reset) plus the always-visible tableau shift the game from luck toward planning.
- The auto-mover is conservative by developer intent; there is no undo and no loss notification.

## Details

### Origin and identity
Sawayama Solitaire is bundled in Last Call BBS as the only game already installed on the in-fiction "Sawayama Z5 Powerlance" computer; the other seven games are downloaded over a simulated dial-up BBS. Zachtronics describes it as "a fresh take on Klondike, the 'classic' solitaire variant," and in the later Zachtronics Solitaire Collection markets it as "A reimagined version of Klondike that is faster, more strategic, and more often winnable than the original."

It is a refinement of an earlier Zachtronics minigame, the Russian-titled "ПАСЬЯНС" in EXAPUNKS (2018). Per the szsol project documentation describing that EXAPUNKS game, ПАСЬЯНС is "played with a 36-card deck of the type popular in Russian card games" that "does not contain joker cards or cards of ranks 2, 3, 4 or 5," with the goal of arranging cards "into 8 separate stacks" (four stacks of 10-to-6 in alternating colors, and four stacks of face cards by suit). The Last Call BBS incarnation is, by contrast, a full 52-card Klondike derivative — so the lineage is real but the rule sets should not be conflated.

### Deck composition
A standard 52-card deck: four suits (hearts, diamonds, spades, clubs), thirteen ranks each (Ace low through King high), with two red suits (hearts, diamonds) and two black suits (spades, clubs). No jokers.

### Setup and layout
The layout mirrors Klondike:
- **Tableau:** Seven columns dealt left-to-right — the first column holding one card, the second two, and so on up to seven in the last column (28 cards total). **Crucially, unlike Klondike, every tableau card is dealt face-up** — nothing is hidden.
- **Stock (deck):** The remaining 24 cards form a face-down stock in the upper-left.
- **Waste/talon:** Cards drawn from the stock are turned up here, three at a time.
- **Foundations:** Four foundation piles (one per suit), built up Ace→King. Players note you cannot manually drag a card into the foundations the way you do in ordinary Klondike — cards advance automatically (with a manual double-click option), which is the single most common point of confusion for newcomers.

A frequently noted quirk: the deal generator appears to filter out certain starting configurations. Players observed Aces essentially never sit exposed in the opening tableau, and a poster in the Steam thread "Is every game of Sawayama Solitaire winnable?" reports that Sawayama "explicitly filters out certain hands based on Ace positions being too much of a problem." Because an Ace exposed on top of a column would instantly auto-advance to a foundation, the practical effect is that the opening avoids leaving Aces in such positions.

### Objective and win condition
Move all 52 cards to the four foundations, each built up by suit from Ace to King. Completing all four foundations wins the game.

### Legal moves and how stacking works
- **Tableau building:** Cards stack in **descending rank and alternating color** (e.g., a red 7 on a black 8) — identical to Klondike.
- **Moving stacks:** A "stack" is "a decreasing series of alternating colors," and any properly ordered run can be moved as a unit of any length; you may split a sequence at any point and move the resulting sub-run. There is no card-count limit on moves (so it does not use FreeCell-style super-move limits).
- **Empty columns:** Any card or ordered stack — not just a King — may be placed into an empty column. This is a major Klondike departure and is FreeCell-like.
- **The free cell:** Sawayama has a single free cell, but it is **locked until the stock is fully exhausted.** Once the last stock card is drawn, the now-empty deck slot permanently becomes a free cell that can hold one card of any type.
- **Foundations:** Build up by suit Ace→King. In the official version, cards placed on the foundations cannot be moved back down onto the tableau.

### The single-pass stock (the defining twist)
Klondike normally lets you cycle the deck repeatedly. Sawayama "always deals three cards at a time, and they never go back into the deck." There is exactly one pass: "The deck can only be drawn through once. After all cards have been drawn, it does not reset." If you deal out the whole stock, you are left with the waste pile from which only the top card is immediately playable, so you must whittle it down one card at a time — but you gain the free cell as compensation. This single-pass rule, combined with the fully visible tableau, is what shifts the game from luck toward planning: because you can see every tableau card you can plan many moves ahead, and you must, since you only get one trip through the stock.

### Special mechanic: the automatic foundation ("auto-mover")
Rather than the player freely sending cards up, Sawayama **automatically advances cards to the foundations**, which is the feature that most confuses players arriving from Klondike. The rule is a "safe autoplay": a card moves up only when the game determines it can no longer be useful in the tableau. As one detailed player explanation puts it, a card moves up "when the game detects there is no more possible use for them. For example, a black 3 will not be moved to the foundation automatically while there is still a red 2 in play, because you can use the 3 to hold the 2. Aces never have any utility so they are always moved automatically." Another player phrased the threshold as moving a card up only once it is no greater than "1 bigger than the smallest opposite color" still in play. Aces, having no holding utility, always auto-advance immediately.

This is deliberate developer design. A Zachtronics patch note for the Solitaire Collection ("UPDATE: Assorted bug fixes") states: "[Sawayama Solitaire] Made the auto-mover more conservative, so that it never causes you to lose a game." Players can also **manually force a card to the foundation with a double-click**, which is sometimes necessary to free up further moves.

### Win/loss conditions
- **Win:** All four foundations completed Ace→King.
- **Loss:** There is no explicit loss state and no out-of-moves warning. As a player summarized in the thread "Playing Sawayama, does the game tell you if you have no valid moves?": "Well, it doesn't tell you when you lost… Actually not even one of all solitaires here does… So you gotta decide yourself when to give up." Combined with the no-redeal, no-undo rules, some deals are simply unwinnable, much like random Klondike.

### Why it plays differently
Removing the hidden cards eliminates blind luck; removing the redeal and undo adds permanent consequence; allowing any card into empty columns and granting a late free cell adds FreeCell-like flexibility. The net effect, widely echoed by players, is a game that demands genuine forward planning yet is often more winnable when played well.

## UI, animations, and visual feedback
- **Retro-computing aesthetic:** The Last Call BBS presentation evokes a 1990s PC, with "high-resolution pixel art and an FM-infused soundtrack." A developer who studied the game described a screenshot of Sawayama as looking like "a retro OS desktop," low-resolution and "slightly warped" so "that it looks like a CRT monitor."
- **Framing fiction:** Sawayama Solitaire is the one game already installed on the Z5 Powerlance; the other games "download" with audible modem sounds, and reviewers praised how "Every UI is designed to make this feel more like finding an old computer, rather than buying a collection of games."
- **Soundtrack and execution:** The game has a dedicated "Sawayama Solitaire" track on the Last Call BBS soundtrack, and the overall execution — "the graphics, the sound effects, the music" — was praised as "flawless."
- **Dealing and move feel:** Cards are dealt into the seven columns face-up; clicking the stock deals three cards at a time to the waste. Cards are moved by dragging single cards or ordered stacks; double-clicking sends a card to the foundation. (Fan recreations reproduce these interactions — drag, or click-to-pick-up/click-to-drop, plus hover-to-read values in some clones — but the original Zachtronics build is the reference for polished, "snappy" feedback.)
- **Wins counter:** The UI tracks lifetime wins. A community thread is titled "Sawayama Solitaire win count doesn't go above 999," implying a cap, but a player video titled "Sawayama Solitaire #1000" directly contradicts it: "Does the counter go up that high? Yes, yes it does." So whether 999 is a hard cap is genuinely disputed. Win totals also feed achievements — for example, winning 10 games of Sawayama Solitaire unlocks the "POWER PLAYER" achievement (50 Gamerscore), with separate achievements for 1 and 100 wins.
- **Rules button:** Each game in the collection includes built-in instructions, accessed (per user "Goblin" on Steam) via "the triangle button left of the wins counter."
- **No undo:** There is deliberately no undo button — a recurring player complaint ("That isn't an undo button and I hate it") — which is part of what makes mistakes permanent.
- **Win animation:** Solitaire games traditionally end with a celebratory bouncing-card cascade; the author of a faithful web clone notes testers insisted "a bouncy card animation after winning a game" was "required in a solitaire game." However, a verbatim, named-source description of the *official* Zachtronics Sawayama win animation specifically was not located, so this point is best-attested for the fan clone and the genre convention.

## Recommendations
- **For a player learning the game:** Open the in-game rules via the triangle button by the wins counter first. Then internalize the three core changes — all cards face-up, draw-three single pass, any card into empty columns — and remember the free cell unlocks only after the stock is gone. Because there is no undo and no redeal, plan several moves ahead before committing; prioritize emptying a whole column early, since any card can refill it, and don't rush mid-rank cards up — the auto-mover already holds them back when they're still useful.
- **For someone documenting or recreating the game:** Treat the canonical ruleset as the five Zachtronics-attributed rules (face-up tableau; any card into empty cells; single deck pass; deck-slot-becomes-free-cell once stock is gone; foundation cards locked). Implement the conservative auto-mover (advance a card only when no lower opposite-color card can still use it; Aces always advance) plus a manual double-click-to-foundation, and omit undo/redeal to match the original.
- **Benchmarks that would change this guidance:** If you can directly inspect the game's deal-generation code or an official rules-screen capture, verify (1) the exact auto-mover threshold wording, (2) whether the wins counter truly caps at 999, and (3) the precise opening-Ace filtering rule — all three currently rest partly on player testimony rather than primary documentation.

## Caveats
- **Source mix:** The most precise rule statements come from the developer-attributed clone README/site (blakewatson / Watson Bros Games, which credits "Sawayama Solitaire rules by Zachtronics"), Zachtronics' own store and patch-note text, and player explanations on Steam forums. Where only fan clones or player testimony exist, that is flagged above.
- **Auto-mover precision:** The "safe autoplay" rule is well-corroborated (two independent player descriptions plus a developer patch note confirming the auto-mover was made "more conservative, so that it never causes you to lose a game"), but the exact internal threshold is described in players' words, not official documentation.
- **999 cap conflict:** Whether the wins counter hard-caps at 999 is disputed between a forum thread title and a player video.
- **Win animation:** The specific official win animation is not documented in a named source; only the general solitaire convention and a clone's implementation are confirmed.
- **EXAPUNKS lineage:** "ПАСЬЯНС" in EXAPUNKS is a related but mechanically distinct ancestor (36-card deck, eight target stacks, no ranks 2–5), so it should not be conflated rule-for-rule with the Last Call BBS Sawayama Solitaire.