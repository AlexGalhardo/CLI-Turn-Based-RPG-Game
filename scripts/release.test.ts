import { describe, expect, test } from "bun:test";
import { bumpFor, changelogSection, nextVersion, releaseChangelog } from "./release";

const CHANGELOG = `# Changelog

## [Unreleased]

### Fixed

- Something.

## [1.2.0] - 2026-10-01

### Added

- Older.

[Unreleased]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.1.0...v1.2.0
`;

describe("release versioning", () => {
	test("the Conventional Commit type decides the bump", () => {
		expect(bumpFor("feat(rust): add x")).toBe("minor");
		expect(bumpFor("fix(golang): y")).toBe("patch");
		expect(bumpFor("docs: z")).toBe("patch");
		expect(bumpFor("feat(cpp)!: drop x")).toBe("major");
		expect(bumpFor("refactor: w", "BREAKING CHANGE: save format")).toBe("major");
	});

	test("bumps reset the lower parts", () => {
		expect(nextVersion("1.3.2", "patch")).toBe("1.3.3");
		expect(nextVersion("1.3.2", "minor")).toBe("1.4.0");
		expect(nextVersion("1.3.2", "major")).toBe("2.0.0");
	});

	test("[Unreleased] becomes the new dated section with compare links", () => {
		const released = releaseChangelog(CHANGELOG, "1.2.0", "1.2.1", "2026-10-03");
		expect(changelogSection(released, "Unreleased")).toBe("");
		expect(changelogSection(released, "1.2.1")).toBe("### Fixed\n\n- Something.");
		expect(changelogSection(released, "1.2.0")).toBe("### Added\n\n- Older.");
		expect(released).toContain(
			"[Unreleased]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.2.1...HEAD",
		);
		expect(released).toContain(
			"[1.2.1]: https://github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/compare/v1.2.0...v1.2.1",
		);
	});

	test("an empty [Unreleased] is refused", () => {
		const empty = CHANGELOG.replace("### Fixed\n\n- Something.\n\n", "");
		expect(() => releaseChangelog(empty, "1.2.0", "1.2.1", "2026-10-03")).toThrow("[Unreleased]");
	});
});
