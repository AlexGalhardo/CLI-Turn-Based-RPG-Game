---
name: release
description: Use before every commit to main in the CLI Turn-Based RPG monorepo — each commit is its own SemVer release (version from the Conventional Commit type), prepared with bun run release:prepare and published by release.yml.
---

# Releasing: one release per commit

Every commit on `main` is a release (`docs/ci-cd.md`). The commit carries its own version and CHANGELOG section;
`.github/workflows/release.yml` only tags it and publishes. Bump: breaking (`type!:` / `BREAKING CHANGE:`) → major,
`feat` → minor, everything else → patch.

## Steps (per commit)

1. Finish and validate the change (format, lint, tests of the touched project).
2. Write what the commit changes under `## [Unreleased]` in `CHANGELOG.md` (Added/Changed/Fixed/Removed/Security).
3. Decide the exact commit subject (its type decides the version), then run
   `bun run release:prepare "<subject>"`. It bumps every version file, moves `[Unreleased]` into
   `## [X.Y.Z] - date` and updates the compare links.
4. `bun run release:check && bun run lint:ci`, then commit with that same subject: the `post-commit` hook tags
   `vX.Y.Z` with the CHANGELOG section as message.
5. `git push` (with `push.followTags true` the tags go along). The `pre-push` hook runs the local CI
   (`scripts/ci-local.sh`) for the touched projects; GitHub workflows are disabled, so no Release page is created.

Several commits in one push: prepare each one before committing it (versions chain: 1.3.1, 1.3.2, …).

## Pitfalls (learned)

- `release:prepare` refuses an empty `[Unreleased]`: the release notes are the section, so write them first.
- A commit pushed without a bump gets no release (warning in the run); there is no way to release it afterwards
  without rewriting history, so never skip step 3.
- A new implementation with its own version file must be added to `VERSION_FILES` in `scripts/release.ts`.
- Rebuild binaries of an existing release: `gh workflow run release.yml -f tag=vX.Y.Z`. Without `tag` the run is a
  dry run (builds everything, publishes nothing) — use it after touching `release.yml`, from a branch with
  `--ref <branch>`.
- The husky `pre-commit` hook runs `gofmt` and `ruff` through lint-staged: Go and uv must be on `PATH` in the shell
  that commits, or the commit is reverted. It also runs `graphify update .` (when installed) and stages
  `graphify-out/`, adding ~15 s per commit.
- Old tag-triggered `release.yml` files still exist in historical commits: before creating or re-creating tags in
  bulk, `gh workflow disable release.yml` and enable it again afterwards.
