/**
 * Per-commit releases (docs/ci-cd.md): every commit on main carries its own SemVer version.
 *
 *   bun run release:prepare "<commit subject>"  bump every version file and turn [Unreleased] into the new section
 *   bun run release:check                       every version file agrees and CHANGELOG.md has that section
 *   bun run scripts/release.ts next "<subject>" print the version the commit will get
 *   bun run scripts/release.ts notes <version> [changelog-path]  print a section body (GitHub Release notes)
 */
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const ROOT = join(import.meta.dir, "..");
const REPO_URL = "https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game";
const SEMVER = String.raw`\d+\.\d+\.\d+`;

interface VersionFile {
	readonly path: string;
	/** Group 1 is the text before the version, group 2 the version itself. */
	readonly pattern: RegExp;
}

// Implementations that do not exist yet in an older checkout are skipped by `check` and `prepare`.
const VERSION_FILES: readonly VersionFile[] = [
	{ path: "package.json", pattern: new RegExp(`^(\\s*"version": ")(${SEMVER})`, "m") },
	{ path: "README.md", pattern: new RegExp(`(badge/version-)(${SEMVER})`) },
	{ path: "rpg-python/pyproject.toml", pattern: new RegExp(`^(version = ")(${SEMVER})`, "m") },
	{ path: "rpg-python/src/rpg/__init__.py", pattern: new RegExp(`^(__version__ = ")(${SEMVER})`, "m") },
	{
		path: "rpg-python/uv.lock",
		pattern: new RegExp(`(\\[\\[package\\]\\]\\nname = "rpg"\\nversion = ")(${SEMVER})`),
	},
	{ path: "rpg-typescript/package.json", pattern: new RegExp(`^(\\s*"version": ")(${SEMVER})`, "m") },
	{ path: "rpg-golang/internal/version/version.go", pattern: new RegExp(`^(const Version = ")(${SEMVER})`, "m") },
	{ path: "rpg-rust/Cargo.toml", pattern: new RegExp(`^(version = ")(${SEMVER})`, "m") },
	{ path: "rpg-rust/Cargo.lock", pattern: new RegExp(`(name = "rpg-rust"\\nversion = ")(${SEMVER})`) },
	{ path: "rpg-rust/src/version.rs", pattern: new RegExp(`(VERSION: &str = ")(${SEMVER})`) },
	{ path: "rpg-elixir/mix.exs", pattern: new RegExp(`(@version ")(${SEMVER})`) },
	{ path: "rpg-elixir/lib/rpg/version.ex", pattern: new RegExp(`(@version ")(${SEMVER})`) },
	{ path: "rpg-cpp/CMakeLists.txt", pattern: new RegExp(`(project\\(rpg_cpp VERSION )(${SEMVER})`) },
	{ path: "rpg-cpp/src/version.hpp", pattern: new RegExp(`(version = ")(${SEMVER})`) },
	{ path: "rpg-asm/src/version.inc", pattern: new RegExp(`(%define VERSION ")(${SEMVER})`) },
];

type Bump = "major" | "minor" | "patch";

export function bumpFor(subject: string, body = ""): Bump {
	if (/^\w+(\([^)]*\))?!:/.test(subject) || /^BREAKING[ -]CHANGE:/m.test(body)) {
		return "major";
	}
	return /^feat(\([^)]*\))?:/.test(subject) ? "minor" : "patch";
}

export function nextVersion(current: string, bump: Bump): string {
	const [major, minor, patch] = current.split(".").map(Number) as [number, number, number];
	if (bump === "major") return `${major + 1}.0.0`;
	if (bump === "minor") return `${major}.${minor + 1}.0`;
	return `${major}.${minor}.${patch + 1}`;
}

/** Body of `## [version]` without its heading, trimmed; undefined when the section is missing. */
export function changelogSection(changelog: string, version: string): string | undefined {
	const lines = changelog.split("\n");
	const start = lines.findIndex((line) => line.startsWith(`## [${version}]`));
	if (start === -1) return undefined;
	const end = lines.findIndex((line, i) => i > start && line.startsWith("## ["));
	return lines
		.slice(start + 1, end === -1 ? undefined : end)
		.filter((line) => !/^\[[^\]]+\]: http/.test(line))
		.join("\n")
		.trim();
}

/** Moves the [Unreleased] entries into a dated `## [version]` section and updates the compare links. */
export function releaseChangelog(changelog: string, previous: string, version: string, date: string): string {
	const unreleased = changelogSection(changelog, "Unreleased");
	if (!unreleased) {
		throw new Error("CHANGELOG.md: describe the change under ## [Unreleased] first");
	}
	return changelog
		.replace(
			/^## \[Unreleased\]\n[\s\S]*?(?=^## \[)/m,
			`## [Unreleased]\n\n## [${version}] - ${date}\n\n${unreleased}\n\n`,
		)
		.replace(
			/^\[Unreleased\]: .*$/m,
			`[Unreleased]: ${REPO_URL}/compare/v${version}...HEAD\n[${version}]: ${REPO_URL}/compare/v${previous}...v${version}`,
		);
}

function read(path: string): string | undefined {
	try {
		return readFileSync(join(ROOT, path), "utf-8");
	} catch {
		return undefined;
	}
}

function currentVersion(): string {
	const match = read("package.json")?.match(VERSION_FILES[0]?.pattern ?? /$^/);
	if (!match?.[2]) throw new Error("package.json has no version");
	return match[2];
}

function check(): string[] {
	const version = currentVersion();
	const errors: string[] = [];
	for (const file of VERSION_FILES) {
		const text = read(file.path);
		if (text === undefined) continue;
		const found = text.match(file.pattern)?.[2];
		if (found !== version) errors.push(`${file.path}: version ${found ?? "not found"}, expected ${version}`);
	}
	if (!changelogSection(read("CHANGELOG.md") ?? "", version)) {
		errors.push(`CHANGELOG.md: no "## [${version}]" section`);
	}
	return errors;
}

function prepare(subject: string): string {
	const previous = currentVersion();
	const version = nextVersion(previous, bumpFor(subject));
	const date = new Date().toISOString().slice(0, 10);
	writeFileSync(join(ROOT, "CHANGELOG.md"), releaseChangelog(read("CHANGELOG.md") ?? "", previous, version, date));
	for (const file of VERSION_FILES) {
		const text = read(file.path);
		if (text === undefined) continue;
		if (!file.pattern.test(text)) throw new Error(`${file.path}: version pattern not found`);
		writeFileSync(join(ROOT, file.path), text.replace(file.pattern, `$1${version}`));
	}
	return version;
}

function main(argv: readonly string[]): number {
	const [command, arg, extra] = argv;
	if (command === "check") {
		const errors = check();
		for (const error of errors) console.error(error);
		if (errors.length === 0) console.log(`version ${currentVersion()} is consistent`);
		return errors.length === 0 ? 0 : 1;
	}
	if (command === "next" && arg) {
		console.log(nextVersion(currentVersion(), bumpFor(arg)));
		return 0;
	}
	if (command === "prepare" && arg) {
		console.log(`prepared ${prepare(arg)}`);
		return 0;
	}
	if (command === "notes" && arg) {
		const notes = changelogSection(readFileSync(extra ?? join(ROOT, "CHANGELOG.md"), "utf-8"), arg);
		if (!notes) {
			console.error(`no "## [${arg}]" section`);
			return 1;
		}
		console.log(notes);
		return 0;
	}
	console.error('usage: release.ts check | next "<subject>" | prepare "<subject>" | notes <version> [changelog]');
	return 2;
}

if (import.meta.main) {
	process.exitCode = main(process.argv.slice(2));
}
