# Go Implementation (`rpg-golang/`)

Phase 3. Written from scratch following the Python reference.

## Stack

- Go 1.27 (`toolchain go1.27.1`)
- [Bubble Tea](https://github.com/charmbracelet/bubbletea) + [Lip Gloss](https://github.com/charmbracelet/lipgloss) for
  the TUI, `teatest` for e2e
- gofmt, go vet, golangci-lint

## Commands

```bash
cd rpg-golang
go generate ./...            # copies ../shared into internal/assets/shared for go:embed
go run ./cmd/rpg
go build -o bin/rpg-golang ./cmd/rpg
gofmt -l . && go vet ./... && golangci-lint run && go test -race -cover ./...
```

## Notes

- `go:embed` can't reach parent directories, so `go generate` copies `shared/` into `internal/assets/shared`
  (git-ignored). CI and the setup scripts run it before building or testing.
- `uint32` arithmetic gives the PRNG wrapping behaviour for free; game math uses `int` (64-bit).
