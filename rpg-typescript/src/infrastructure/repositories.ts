/** JSON file repositories under the player's data directory (docs/persistence.md). */
import { existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import type { Clock, HistoryRepository, ProfileRepository, SaveRepository } from "../application/ports";
import { Profile } from "../application/profile";
import {
	checkSchema,
	type RunRecord,
	runRecordFromJson,
	runRecordToJson,
	SaveGame,
	SCHEMA_VERSION,
} from "../application/save-game";
import { type JsonObject, type JsonValue, jsonBool, jsonInt, jsonObj, jsonStr } from "../domain/json-types";
import { SUPPORTED_LOCALES } from "./i18n";
import { migrateHistory, migrateProfile, migrateSave, migrateSettings } from "./migrations";

export const BATTLE_SPEEDS: readonly number[] = [1, 2];

/** Write to a temp file then rename, so a crash never leaves a half-written save. */
export function writeJsonAtomic(path: string, document: JsonObject): void {
	mkdirSync(dirname(path), { recursive: true });
	const temporary = `${path}.tmp`;
	writeFileSync(temporary, `${JSON.stringify(document, null, "\t")}\n`, "utf-8");
	renameSync(temporary, path);
}

export function readJson(path: string): JsonValue {
	return JSON.parse(readFileSync(path, "utf-8")) as JsonValue;
}

export class SystemClock implements Clock {
	now(): Date {
		return new Date();
	}
}

export interface Settings {
	readonly locale: string | null;
	readonly autoEquip: boolean;
	readonly battleSpeed: number;
}

export const DEFAULT_SETTINGS: Settings = { locale: null, autoEquip: false, battleSpeed: 1 };

export class SettingsRepository {
	readonly #path: string;

	constructor(dataDir: string) {
		this.#path = join(dataDir, "settings.json");
	}

	load(): Settings {
		if (!existsSync(this.#path)) return DEFAULT_SETTINGS;
		let data = jsonObj(readJson(this.#path));
		checkSchema(data, "settings.json");
		data = migrateSettings(data);
		const locale = data.locale === undefined ? null : jsonStr(data.locale);
		const speed = jsonInt(data.battleSpeed);
		return {
			locale: locale !== null && SUPPORTED_LOCALES.includes(locale) ? locale : null,
			autoEquip: jsonBool(data.autoEquip),
			battleSpeed: BATTLE_SPEEDS.includes(speed) ? speed : (BATTLE_SPEEDS[0] ?? 1),
		};
	}

	save(settings: Settings): void {
		const document: JsonObject = { schemaVersion: SCHEMA_VERSION };
		if (settings.locale !== null) document.locale = settings.locale;
		document.autoEquip = settings.autoEquip;
		document.battleSpeed = settings.battleSpeed;
		writeJsonAtomic(this.#path, document);
	}
}

export class FileSaveRepository implements SaveRepository {
	readonly #path: string;

	constructor(dataDir: string) {
		this.#path = join(dataDir, "save.json");
	}

	load(): SaveGame | null {
		if (!existsSync(this.#path)) return null;
		const document = jsonObj(readJson(this.#path));
		checkSchema(document, "save.json");
		return SaveGame.fromJson(migrateSave(document));
	}

	save(save: SaveGame): void {
		writeJsonAtomic(this.#path, save.toJson());
	}

	delete(): void {
		rmSync(this.#path, { force: true });
	}
}

export class FileHistoryRepository implements HistoryRepository {
	readonly #dir: string;

	constructor(dataDir: string) {
		this.#dir = join(dataDir, "history");
	}

	add(record: RunRecord): void {
		writeJsonAtomic(join(this.#dir, `${record.runId}.json`), runRecordToJson(record));
	}

	list(): RunRecord[] {
		if (!existsSync(this.#dir)) return [];
		return readdirSync(this.#dir)
			.filter((file) => file.endsWith(".json"))
			.sort()
			.map((file) => {
				const document = jsonObj(readJson(join(this.#dir, file)));
				checkSchema(document, "history record");
				return runRecordFromJson(migrateHistory(document));
			});
	}
}

export class FileProfileRepository implements ProfileRepository {
	readonly #path: string;

	constructor(dataDir: string) {
		this.#path = join(dataDir, "profile.json");
	}

	load(): Profile {
		if (!existsSync(this.#path)) return new Profile();
		const data = jsonObj(readJson(this.#path));
		checkSchema(data, "profile.json");
		return Profile.fromJson(migrateProfile(data));
	}

	save(profile: Profile): void {
		writeJsonAtomic(this.#path, profile.toJson());
	}
}
