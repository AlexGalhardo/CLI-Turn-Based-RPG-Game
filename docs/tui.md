# Terminal UI

Full-screen TUI with equivalent frameworks: **Textual** (Python), **Ink** (TypeScript/React), **Bubble Tea + Lip
Gloss** (Go). The layout below is the spec; the three must look practically identical at 100 × 30.

## UI controller (shared design)

The screens, their options, key bindings and texts live in a **framework-independent controller**
(`presentation/controller.py` in Python; the same module name in TypeScript and Go). The TUI framework only renders
the controller's state: `header()`, `monster_view()`, `player_view()`, the combat log, `title()`, `body_lines()`,
`options()` and `input_prompt()`, and forwards key presses to `press(key)`. Porting the UI = porting the controller
(unit-testable without a terminal) + a thin renderer.

- List screens use keys `1-9` then `a-z` (`render.list_key`).
- Informative screens (character, Hall of Fame, bestiary, achievements) hide the combat log and page their lines
  (10 per page, `N`/`P`).
- Menus with more than 4 options render in two columns.

## Battle screen

```
┌─ Round 7 · Tier 1 · NORMAL ─────────────────────────────────────── Seed 42 ─┐
│         (o.o)             ORC WARLORD                                       │
│        <|   |>            HP ██████████████░░░░░░  812/950                  │
│         /   \             physical · weak: fire · burn(2)                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ Alex · Warrior · Lv 12 · ML 4                         Gold 1,240             │
│ HP ████████████████████░░░░░  1450/1800                                     │
│ MP ██████░░░░░░░░░░░░░░░░░░░   120/450                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ › Brutal Strike hits Orc Warlord for 184 (CRITICAL!)                        │
│ › Orc Warlord hits you for 97.                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│ [1] Attack   [2] Spells   [3] Potions   [4] Defend          [Q] Save & quit │
└─────────────────────────────────────────────────────────────────────────────┘
```

- **Bars**: 25 cells, `█` filled / `░` empty, filled cells = `floor(25 * current / max)` (at least 1 if current > 0).
  HP colour: green > 50%, yellow > 25%, red otherwise. MP: blue.
- **Combat log** keeps the last 50 lines, shows the newest that fit.
- **Minimum terminal size** 100 × 30; smaller terminals show a "please resize" message.

## Screens

| Screen | Keys |
|---|---|
| Title | `[1]` Continue (only with a save) · `[2]` New run · `[3]` Hall of Fame · `[4]` Bestiary · `[5]` Achievements · `[6]` Language · `[0]` Quit |
| Language (first launch and from title) | `[1]` English · `[2]` Português (Brasil) |
| new run | difficulty `[1-3]` → name (text input, 1–16 chars) → vocation `[1-3]` |
| Battle | `[1]` Attack · `[2]` Spells submenu · `[3]` Potions submenu · `[4]` Defend · `[Q]` Save & quit |
| Merchant | `[1]` Buy potions · `[2]` Sell items · `[3]` Equipment · `[4]` Merchant stock · `[5]` Character · `[0]` Next fight · `[Q]` Save & quit |
| Game over | run summary + `[1]` New run · `[2]` Title |

- **Direct number keys** (no Enter) trigger options; arrows + Enter also navigate lists; `0`/`Esc` goes back.
- Every string comes from i18n (`shared/i18n`).
- No emoji in the UI: their display width differs between terminals and would break the aligned layout.

## Animation

- Monster art is read from `shared/art` (see [data-format.md](data-format.md)).
- `idle` loops at 500 ms per frame; `attack` plays once when the monster attacks; `hurt` plays once (red tint) when it
  takes damage; bosses flash on `boss_telegraph`.
- Colour by the monster's main attack element: physical white, fire red, ice cyan, energy magenta, earth green, holy
  yellow, death grey.
- `--no-anim` (or `RPG_NO_ANIM=1`) disables timers: first frame only. Tests always run without animation.

## CLI flags (identical in the three binaries)

```
--seed <n>          deterministic run
--lang <en|pt-BR>   override the saved language
--no-anim           disable animations
--data-dir <path>   saves/profile location
--simulate <n>      run n headless bot games and print a balance report
  --vocation <id>   (simulator) restrict to one vocation
  --difficulty <id> (simulator) restrict to one difficulty
--version / --help
```
