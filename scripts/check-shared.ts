/**
 * Validates the language-agnostic content in shared/ (run with `bun run check:shared`):
 * JSON Schemas for shared/data, identical key sets across shared/i18n locales, and the ASCII art format.
 */
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import Ajv2020 from "ajv/dist/2020";

const SHARED = join(import.meta.dir, "..", "shared");
const ART_MAX_COLUMNS = 32;
const ART_MAX_ROWS = 10;
const ART_REQUIRED_ANIMATIONS = ["idle", "attack", "hurt"];

const errors: string[] = [];

function readJson(path: string): unknown {
	return JSON.parse(readFileSync(path, "utf-8"));
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function checkSchemas(): void {
	const ajv = new Ajv2020({ allErrors: true, strict: true });
	for (const file of readdirSync(join(SHARED, "schemas"))) {
		const name = file.replace(".schema.json", "");
		const dataPath = join(SHARED, "data", `${name}.json`);
		if (!existsSync(dataPath)) {
			continue;
		}
		const validate = ajv.compile(readJson(join(SHARED, "schemas", file)) as object);
		if (!validate(readJson(dataPath))) {
			for (const error of validate.errors ?? []) {
				errors.push(`data/${name}.json${error.instancePath}: ${error.message ?? "invalid"}`);
			}
		}
	}
	for (const file of readdirSync(join(SHARED, "data"))) {
		if (!existsSync(join(SHARED, "schemas", file.replace(".json", ".schema.json")))) {
			errors.push(`data/${file}: missing schema in shared/schemas`);
		}
	}
}

function checkI18n(): void {
	const directory = join(SHARED, "i18n");
	if (!existsSync(directory)) {
		return;
	}
	const locales = readdirSync(directory).filter((file) => file.endsWith(".json"));
	const keySets = new Map<string, Set<string>>();
	for (const file of locales) {
		const document = readJson(join(directory, file));
		if (!isRecord(document)) {
			errors.push(`i18n/${file}: must be a flat object`);
			continue;
		}
		for (const [key, value] of Object.entries(document)) {
			if (typeof value !== "string") {
				errors.push(`i18n/${file}: ${key} must be a string`);
			}
		}
		keySets.set(file, new Set(Object.keys(document)));
	}
	const reference = keySets.get("en.json");
	if (reference === undefined) {
		if (locales.length > 0) {
			errors.push("i18n/en.json is required (default locale)");
		}
		return;
	}
	for (const [file, keys] of keySets) {
		for (const key of reference) {
			if (!keys.has(key)) errors.push(`i18n/${file}: missing key ${key}`);
		}
		for (const key of keys) {
			if (!reference.has(key)) errors.push(`i18n/${file}: extra key ${key} (not in en.json)`);
		}
	}
}

function checkArtFile(path: string, label: string): void {
	const animations = new Map<string, string[][]>();
	let current: string[][] | undefined;
	for (const line of readFileSync(path, "utf-8").replace(/\n$/, "").split("\n")) {
		if (line.startsWith("@")) {
			current = [[]];
			animations.set(line.slice(1).trim(), current);
		} else if (line === "%%") {
			current?.push([]);
		} else if (current === undefined) {
			errors.push(`${label}: content before the first @animation`);
			return;
		} else {
			current[current.length - 1]?.push(line);
		}
	}
	for (const name of ART_REQUIRED_ANIMATIONS) {
		if (!animations.has(name)) errors.push(`${label}: missing @${name}`);
	}
	for (const [name, frames] of animations) {
		frames.forEach((frame, index) => {
			if (frame.length === 0 || frame.length > ART_MAX_ROWS) {
				errors.push(`${label} @${name} frame ${index}: must have 1-${ART_MAX_ROWS} rows`);
			}
			for (const row of frame) {
				if (row.includes("\t")) errors.push(`${label} @${name}: tabs are not allowed`);
				if ([...row].length > ART_MAX_COLUMNS) {
					errors.push(`${label} @${name}: row longer than ${ART_MAX_COLUMNS} columns`);
				}
			}
		});
	}
}

function checkArt(): void {
	const familiesDir = join(SHARED, "art", "families");
	const bossesDir = join(SHARED, "art", "bosses");
	const hasArt = existsSync(familiesDir) && readdirSync(familiesDir).some((file) => file.endsWith(".txt"));
	if (!hasArt) {
		return;
	}
	const families = readJson(join(SHARED, "data", "families.json"));
	const bosses = readJson(join(SHARED, "data", "bosses.json"));
	if (
		!isRecord(families) ||
		!Array.isArray(families.families) ||
		!isRecord(bosses) ||
		!Array.isArray(bosses.bosses)
	) {
		errors.push("art: families.json/bosses.json have an unexpected shape");
		return;
	}
	for (const family of families.families) {
		const path = join(familiesDir, `${String(family)}.txt`);
		if (existsSync(path)) checkArtFile(path, `art/families/${String(family)}.txt`);
		else errors.push(`art/families/${String(family)}.txt: missing`);
	}
	for (const boss of bosses.bosses) {
		const id = isRecord(boss) ? String(boss.id) : "?";
		const path = join(bossesDir, `${id}.txt`);
		if (existsSync(path)) checkArtFile(path, `art/bosses/${id}.txt`);
		else errors.push(`art/bosses/${id}.txt: missing`);
	}
}

checkSchemas();
checkI18n();
checkArt();

if (errors.length > 0) {
	console.error(`shared/ check failed with ${errors.length} error(s):`);
	for (const error of errors) console.error(`  - ${error}`);
	process.exit(1);
}
console.log("shared/ is valid: schemas, i18n keys and art");
