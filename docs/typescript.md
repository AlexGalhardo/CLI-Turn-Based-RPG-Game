# TypeScript Implementation (`rpg-typescript/`)

Phase 2. A port of the Python reference with the same layers, file names and behaviour. It passes every
`shared/golden` scenario, its bot chooses the same commands as the Python bot, and its run state serialises to the same
JSON (saves are interchangeable).

## Stack

- TypeScript 7 (native compiler, `strict`, `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`)
- Bun 1.4.2 (runtime, package manager, test runner with coverage, `--compile` bundler)
- [Ink](https://github.com/vadimdemedes/ink) 7 + React 19 for the TUI, `ink-testing-library` for e2e
- Biome (format + lint, extends the root `biome.json`)

## Commands

```bash
cd rpg-typescript
bun install
bun run dev -- --seed 42     # play from source (flags after --)
bun run build                # single-file executable → dist/rpg-typescript(.exe)
bun run lint && bun run typecheck && bun run test:coverage
```

## Structure

```
rpg-typescript/src/
├── main.tsx                 # entry point (flags → simulator or Ink app)
├── version.ts               # reads package.json
├── domain/                  # rng, enums, json-types, definitions, formulas, entities, character
├── application/             # engine, battle, merchant, loot, spawner, progression, statistics, run-state,
│                            # save-game, profile, game-session, ports, bot, simulator, commands, events,
│                            # auto-equip (auto-equip + auto-sell), auto-battle (policy)
├── infrastructure/          # embedded-shared (static imports of shared/), data-loader, i18n, art, repositories, paths,
│                            # migrations (schema 1 → 2)
├── presentation/            # cli, event-text, render, controller, simulator-report, tui/app.tsx (Ink)
└── stubs/react-devtools-core.ts
tests/{unit,integration,golden,e2e}/ + helpers.ts
```

## Notes

- **Shared files are embedded**: `infrastructure/embedded-shared.ts` imports every JSON and art file statically, so the
  compiled binary needs no checkout. A new monster family or boss needs one import line there.
- Integer math: always `Math.floor` after multiplications/divisions; the PRNG uses `Math.imul` and `>>> 0`.
- No `any`: untrusted JSON is read through `json-types.ts` narrowing helpers.
- Auto-battle pacing lives in the Ink app (`tui/app.tsx`): a `setInterval` of `controller.autoBattleIntervalMs()`
  (600 ms at 1x, 300 ms at 2x) calls `autoBattleStep()` until it returns `false`; with `--no-anim` the app calls
  `runAutoBattle()` and the fight resolves instantly.
- Ink imports the optional `react-devtools-core` peer only when `DEV=true`, but `bun build --compile` still needs to
  resolve it: `tsconfig.json` maps it to `src/stubs/react-devtools-core.ts`.
- In e2e tests, a lone `ESC` needs ~100 ms before Ink emits it (it may start an escape sequence).
