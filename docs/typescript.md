# TypeScript Implementation (`rpg-typescript/`)

Phase 2. A port of the Python reference with the same layers, names and behaviour.

## Stack

- TypeScript 7 (native compiler) with `strict`, Bun 1.4.2 (runtime, package manager, test runner, bundler)
- [Ink](https://github.com/vadimdemedes/ink) + React for the TUI, `ink-testing-library` for e2e
- Biome (format + lint)

## Commands

```bash
cd rpg-typescript
bun install
bun run dev                  # play from source
bun run build                # single-file executable → ./dist/rpg-typescript(.exe)
bun run lint && bun run typecheck && bun test
```

## Notes

- Shared JSON and art are imported so `bun build --compile` embeds them in the executable.
- Integer math: always `Math.floor` after multiplications/divisions; the PRNG uses `Math.imul` and `>>> 0`.
- No `any`: unknown JSON is parsed as `unknown` and narrowed by validators.
