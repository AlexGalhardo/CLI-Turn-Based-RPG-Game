# Terminal UI

Full-screen TUI with equivalent frameworks: **Textual** (Python), **Ink** (TypeScript/React), **Bubble Tea + Lip
Gloss** (Go), **ratatui + crossterm** (Rust), a **hand-written ANSI renderer** with raw keyboard input (Elixir, no
dependencies) and **FTXUI** (C++). The layout below is the spec; the six must look practically identical at 100 × 30.

## UI controller (shared design)

The screens, their options, key bindings and texts live in a **framework-independent controller**
(`presentation/controller.py` in Python; the same module name in the other five languages). The TUI framework only
renders the controller's state: `header()`, `monster_view()`, `player_view()`, the combat log, `title()`,
`body_lines()`, `options()` and `input_prompt()`, and forwards key presses to `press(key)`. Porting the UI = porting the
controller (unit-testable without a terminal) + a thin renderer.

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
| Title | `[1]` Continue (only with a save) · `[2]` New run · `[3]` Hall of Fame · `[4]` Bestiary · `[5]` Achievements · `[6]` Settings · `[0]` Quit |
| Language (first launch and from Settings) | `[1]` English · `[2]` Português (Brasil) |
| Settings | `[1]` Language (returns to Settings) · `[2]` Auto-equip default (on/off, toggles) · `[3]` Battle speed (1x/2x, toggles) · `[0]` Back; every change is saved at once |
| new run | difficulty `[1-3]` → name (text input, 1–16 chars) → vocation `[1-3]` → auto-equip `[1]` On · `[2]` Off · `[0]` Back (the settings default is marked "(default)") |
| Battle | `[1]` Attack · `[2]` Spells submenu · `[3]` Potions submenu · `[4]` Defend · `[5]` Auto-battle · `[Q]` Save & quit |
| Auto-battle | `[1]` Weapon focus · `[2]` Spell focus · `[3]` Balanced · `[0]` Back; once started (log line "Auto-battle (mode): the fight plays itself."), the fight plays itself (one turn every 600 ms at 1x, 300 ms at 2x, instantly with `--no-anim`) and keys are ignored until it ends; the controller exposes `auto_battle_active`, `auto_battle_step()` (one turn), `run_auto_battle()` (instant) and `auto_battle_interval_ms()`, and gives control back after 10 000 turns as a safety net |
| Merchant | `[1]` Buy potions · `[2]` Sell items · `[3]` Equipment · `[4]` Merchant stock · `[5]` Character · `[0]` Next fight · `[Q]` Save & quit |
| Victory | final boss beaten: run summary + `[1]` End run (saved as won) · `[2]` Continue (endless); "Continue run" from the title returns here when the save is in the `victory` phase |
| Game over | title "GAME OVER" (or "RUN COMPLETE" for a won run) + run summary (won or cause of death) + `[1]` New run · `[2]` Title |

- **Direct number keys** (no Enter) trigger options; arrows + Enter also navigate lists; `0`/`Esc` goes back.
- Every string comes from i18n (`shared/i18n`).
- No emoji in the UI: their display width differs between terminals and would break the aligned layout.
- After a victory the combat log lists auto-equip results, one line per event: "Auto-equipped X (score N)." then
  "Sold Y for G gold (auto-sell)." (the same text is used for a drop sold because the bag is full).
- Elite monsters show an `ELITE` tag (bold yellow) before their name, like the `BOSS` tag; their round starts with
  "Round N: an ELITE X appears!". Won runs are marked `WON` in the Hall of Fame.

## Equipment screen (ARPG style)

Modelled on Diablo IV / Last Epoch: the character's slots at a glance, then a comparison before every swap.

```
 EQUIPPED · total score 412
 Weapon: Fire Sword [Legendary] · Lv 9 · score 180
 Shield: - empty -
 Helmet: Leather Helmet [Common] · Lv 1 · score 24
 Armor: - empty -
 ...                                       (all 8 slots, fixed order)
 BAG (usable)
 [1] Serpent Sword [Rare] · Weapon · Lv 5 · score 140  -40
 [2] Plate Armor [Mythic] · Armor · Lv 13 · score 260 · requires Lv 13  +260
 [3] Weapon: Fire Sword [Legendary]
 [4] Helmet: Leather Helmet [Common]
 [0] Back
```

- Every slot is listed in the order `weapon, shield, helmet, armor, legs, boots, ring, amulet`; empty slots are shown
  as `- empty -` in the warning colour, so missing pieces stand out. The header shows the total score.
- The bag list (options `[1]..`) shows each usable item with rarity colour, required level, score and the score
  delta against the item in its slot (`+N`/`-N`/`0`, green when higher, red when lower, an empty slot counts as 0);
  items above the player's level are dimmed with "requires Lv N". The controller returns the delta as the option's
  `detail` with its own colour (`MenuOption.detail` / `detail_color`), and body lines with colours
  (`body_colors()`: `warning`, `gain`, `loss`, `dim` or a rarity id).
- The equipped items are selectable too, after the bag items (the keys continue), labelled `Slot: Name [Rarity]`.
- Selecting a bag item opens the **comparison** (title `Slot: current → new`): one line per stat of either item in stat
  order, `Stat: current → new (+/-delta)`, gains in green and losses in red, "Affixes gained" (the new item's affixes,
  green) and "Affixes lost" (the current item's affixes, red), `Score: current → new (delta)` and, when the item is above
  the player's level, "Requires level N (you are level L)." in red; `[1]` Equip · `[0]` Back. Both return to the
  equipment screen (an error such as `level_too_low` is shown as the message).
- Selecting an equipped slot shows the item (name, rarity, level, score, stats) and offers `[1]` Unequip · `[0]` Back.

## Animation

- Monster art is read from `shared/art` (see [data-format.md](data-format.md)).
- `idle` loops at 500 ms per frame; `attack` plays once when the monster attacks; `hurt` plays once (red tint) when it
  takes damage; bosses flash on `boss_telegraph`.
- Colour by the monster's main attack element: physical white, fire red, ice cyan, energy magenta, earth green, holy
  yellow, death grey.
- `--no-anim` (or `RPG_NO_ANIM=1`) disables timers: first frame only. Tests always run without animation.

## CLI flags (identical in the six implementations)

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
