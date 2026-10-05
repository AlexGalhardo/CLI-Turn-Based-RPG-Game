# Releases and binaries

- Every commit on `main` is a release (`release` skill, `docs/ci-cd.md`). GitHub Actions are disabled on purpose:
  never re-enable `ci.yml` or `release.yml` to build or publish anything.
- After pushing a release commit (its `vX.Y.Z` tag goes with `push.followTags`), publish the GitHub Release with
  binaries from this machine: `bash scripts/release-local.sh vX.Y.Z` (Git Bash on Windows, Docker running, `gh`
  authenticated). It builds from the tag, smoke-tests, writes `SHA256SUMS.txt`, creates the Release page if missing and
  uploads with `--clobber`. `--no-upload` builds into `dist-release/` only.
- To check whether GitHub is behind: `git describe --tags --exact-match HEAD` vs `gh release list --limit 3`; every
  missing tag needs its Release page, and the newest one (marked Latest) must list the 14 binaries + `SHA256SUMS.txt`
  (`gh release view vX.Y.Z --json assets -q '.assets[].name'`).
- Rust linux/darwin and C++ darwin (Docker, zig) are optional steps not validated yet; when one is missing, fix it
  and record the result in `docs/ci-cd.md`. Pull images one at a time: parallel pulls hung Docker Desktop.
- A new asset or toolchain change goes in `scripts/release-local.sh` and the asset table of `docs/ci-cd.md` together.
