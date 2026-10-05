---
name: release
description: Use before every commit to main in the CLI Turn-Based RPG monorepo — each commit is its own SemVer release (version from the Conventional Commit type), prepared with bun run release:prepare and published with binaries by scripts/release-local.sh (GitHub Actions are disabled).
---

# Releasing: one release per commit

Every commit on `main` is a release (`docs/ci-cd.md`). The commit carries its own version and CHANGELOG section;
the `post-commit` hook tags it and `scripts/release-local.sh` publishes the GitHub Release with binaries. Bump: breaking (`type!:` / `BREAKING CHANGE:`) → major,
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
6. `bash scripts/release-local.sh vX.Y.Z` (Docker running): builds the 14 binaries + `SHA256SUMS.txt` from the tag,
   creates the GitHub Release if missing and uploads them. Check `gh release view vX.Y.Z` lists the assets.

Several commits in one push: prepare each one before committing it (versions chain: 1.3.1, 1.3.2, …).

## Pitfalls (learned)

- `release:prepare` refuses an empty `[Unreleased]`: the release notes are the section, so write them first.
- A commit pushed without a bump gets no release (warning in the run); there is no way to release it afterwards
  without rewriting history, so never skip step 3.
- A new implementation with its own version file must be added to `VERSION_FILES` in `scripts/release.ts`.
- Never re-enable `release.yml` to get binaries (the user wants no GitHub Actions); rebuild an existing release with
  `bash scripts/release-local.sh vX.Y.Z` (uploads with `--clobber`). `--no-upload` is the dry run.
- Several releases pushed at once (or tags pushed while the script was not run): create the missing Release pages
  too — the script does it per tag; only the newest one needs binaries, but running it for each is harmless.
- Windows-only pieces: the Rust/C++ `windows-x64.exe` assets are built on the host, so run the script from Windows
  (Git Bash). On Linux/macOS it warns and skips them.
- Docker Desktop on Windows: volume paths go through `cygpath -m` and `MSYS_NO_PATHCONV=1`, or Git Bash mangles them.
- PowerShell 5.1 has no `&&`: run the script from Git Bash (`bash scripts/release-local.sh …`).
- The husky `pre-commit` hook runs `gofmt` and `ruff` through lint-staged: Go and uv must be on `PATH` in the shell
  that commits, or the commit is reverted. It also runs `graphify update .` (when installed) and stages
  `graphify-out/`, adding ~15 s per commit.
- Old tag-triggered `release.yml` files still exist in historical commits: before creating or re-creating tags in
  bulk, `gh workflow disable release.yml` and enable it again afterwards.
