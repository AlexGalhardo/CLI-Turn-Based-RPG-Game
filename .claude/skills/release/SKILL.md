---
name: release
description: Cut a new version of the monorepo (SemVer + Keep a Changelog + tag). Use when a milestone in PLAN.md is done or the user asks for a release/version bump.
---

# Releasing a version

One version for the whole monorepo (`docs/ci-cd.md`). A release is a single `chore(release): vX.Y.Z` commit on top of
green feature commits, followed by an annotated tag that triggers `.github/workflows/release.yml`.

## Steps

1. Make sure `main` is green (`gh run list --branch main --limit 1`) and the working tree only holds release changes.
2. Bump the version in **all six places** (grep the old version to be sure nothing is missed):
   - `package.json` (root)
   - `rpg-python/pyproject.toml` and `rpg-python/src/rpg/__init__.py`, then `uv lock` (the lockfile stores it too)
   - `rpg-typescript/package.json` (read by `src/version.ts`)
   - `rpg-golang/internal/version/version.go`
   - the version badge in `README.md`
3. `CHANGELOG.md`: move the `[Unreleased]` entries into `## [X.Y.Z] - YYYY-MM-DD` (Added/Changed/Fixed/Removed) and
   update the compare links at the bottom (`[Unreleased]` → `vX.Y.Z...HEAD`, new `[X.Y.Z]` → `vPREV...vX.Y.Z`).
   `release.yml` fails if the section is missing: it uses the section as the GitHub Release notes.
4. Tick the milestone in `PLAN.md` (status table + checklist).
5. Verify: `bun run lint:ci && bun run check:shared`, each implementation's `--version` prints the new version.
6. Commit `chore(release): vX.Y.Z`, push, wait for CI (`gh run watch <id> --exit-status`).
7. Only when CI is green: `git tag -a vX.Y.Z -m "vX.Y.Z — <summary>"` and `git push origin vX.Y.Z`.
   `release.yml` cross-compiles the TypeScript and Go binaries (linux-x64, darwin-arm64, windows-x64), writes
   `SHA256SUMS.txt` and creates the GitHub Release.

## Pitfalls (learned)

- The husky `pre-commit` hook runs `gofmt` and `ruff` through lint-staged: Go and uv must be on `PATH` in the shell
  that commits, or the commit is reverted.
- Never tag before CI is green; a tag cannot be "fixed" without deleting a published release.
- Keep the scripted edits small (Edit tool or a Python script file); long shell heredocs with quotes have failed here.
