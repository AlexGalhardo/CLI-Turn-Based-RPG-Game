# Go Implementation (`rpg-golang/`)

Phase 3. Written from scratch following the Python reference: same layers, file names and behaviour. It passes every
`shared/golden` scenario, its bot issues exactly the commands recorded by the Python bot, and its run state serialises
to the same JSON (saves are interchangeable with Python and TypeScript).

## Stack

- Go 1.27 (`go.mod`), standard library first
- [Bubble Tea v2](https://github.com/charmbracelet/bubbletea) + [Lip Gloss v2](https://github.com/charmbracelet/lipgloss)
  (`charm.land/*/v2`) for the TUI
- gofmt/gofumpt, go vet and golangci-lint v2.14 with the configuration recommended by the `golang-lint` skill
  (`.golangci.yml`, with documented exclusions)
- Project skills from [samber/cc-skills-golang](https://github.com/samber/cc-skills-golang) in `.claude/skills/golang-*`
  (start with `golang-how-to`, which routes to the others)

## Commands

```bash
cd rpg-golang
go generate ./...            # copies ../shared (data, i18n, art) into internal/assets/shared for go:embed
go run ./cmd/rpg --seed 42
go build -o bin/rpg-golang ./cmd/rpg
gofmt -l . && go vet ./... && golangci-lint run ./...
go test -race -coverpkg=./internal/... -coverprofile=coverage.out ./...   # -race needs cgo (gcc) locally
```

## Structure

```
rpg-golang/
├── cmd/rpg/main.go                 # flags → simulator or Bubble Tea program (run() is testable)
├── internal/
│   ├── assets/                     # go:embed of the synced shared/ tree (generated, git-ignored)
│   ├── domain/                     # rng, enums, definitions, formulas, entities, character
│   ├── application/                # engine, battle, merchant, loot, spawner, progression, statistics, run_state,
│   │                               # save_game, profile, game_session (+ ports), bot, simulator, commands, events
│   ├── infrastructure/             # data_loader, i18n, art, repositories (+ clock), paths
│   ├── presentation/               # cli, event_text, render, controller (+ controller_body), simulator_report
│   │   └── tui/app.go              # Bubble Tea model rendering the controller
│   └── version/
└── tools/syncshared/               # used by go generate
```

## Notes

- `go:embed` can't reach parent directories, so `go generate` copies `shared/` into `internal/assets/shared`
  (git-ignored). CI and the setup scripts run it before building or testing.
- `uint32` arithmetic gives the PRNG wrapping behaviour for free; game math uses `int` (64-bit).
- Never iterate a map to make a game decision (Go randomises map order): definitions keep slices in file order, and
  maps are only used for lookups by id. JSON maps marshal with sorted keys, which matches the reference saves.
- Events are `map[string]any` so they compare structurally with the golden files; commands render with `ToMap()`.
- E2E tests drive the real Bubble Tea model through `Update`/`View` (Elm architecture), including a whole run by keys.
- Problems caused by the player become `error` events; `panic` is reserved for inconsistent data (a lookup of an id the
  data itself references), which the data tests rule out.
