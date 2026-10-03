/** Save file and finished-run record formats, shared by the six implementations (docs/persistence.md). */
import { field, type JsonObject, type JsonValue, jsonBool, jsonInt, jsonObj, jsonStr } from "../domain/json-types";
import { RunState } from "./run-state";
import { RunStatistics } from "./statistics";

export const SCHEMA_VERSION = 2;
export const IMPLEMENTATION = "typescript";

const pad = (value: number, size = 2): string => String(value).padStart(size, "0");

export function formatTimestamp(moment: Date): string {
	return (
		`${moment.getUTCFullYear()}-${pad(moment.getUTCMonth() + 1)}-${pad(moment.getUTCDate())}` +
		`T${pad(moment.getUTCHours())}:${pad(moment.getUTCMinutes())}:${pad(moment.getUTCSeconds())}Z`
	);
}

export function parseTimestamp(text: string): Date {
	const match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})Z$/.exec(text);
	if (match === null) throw new RangeError(`invalid timestamp: ${text}`);
	const [, year, month, day, hour, minute, second] = match.map(Number);
	return new Date(Date.UTC(year ?? 0, (month ?? 1) - 1, day, hour, minute, second));
}

export function makeRunId(startedAt: Date, seed: number): string {
	return `${formatTimestamp(startedAt).replaceAll("-", "").replaceAll(":", "")}-${seed}`;
}

/** The file was written by a newer game version; it is never overwritten. */
export class NewerSchemaError extends Error {}

export function checkSchema(data: JsonObject, what: string): void {
	const version = jsonInt(field(data, "schemaVersion"));
	if (version > SCHEMA_VERSION) {
		throw new NewerSchemaError(`${what} uses schema ${version}; update the game (supports ${SCHEMA_VERSION})`);
	}
}

export class SessionInfo {
	constructor(
		readonly runId: string,
		readonly startedAt: string,
		public playTimeSeconds = 0,
		public sessions = 1,
	) {}

	toJson(): JsonObject {
		return {
			runId: this.runId,
			startedAt: this.startedAt,
			playTimeSeconds: this.playTimeSeconds,
			sessions: this.sessions,
		};
	}

	static fromJson(raw: JsonValue): SessionInfo {
		const data = jsonObj(raw);
		return new SessionInfo(
			jsonStr(field(data, "runId")),
			jsonStr(field(data, "startedAt")),
			jsonInt(field(data, "playTimeSeconds")),
			jsonInt(field(data, "sessions")),
		);
	}
}

export class SaveGame {
	constructor(
		readonly gameVersion: string,
		readonly implementation: string,
		readonly savedAt: string,
		readonly rngState: number,
		readonly session: SessionInfo,
		readonly run: RunState,
	) {}

	toJson(): JsonObject {
		return {
			schemaVersion: SCHEMA_VERSION,
			gameVersion: this.gameVersion,
			implementation: this.implementation,
			savedAt: this.savedAt,
			rngState: this.rngState,
			session: this.session.toJson(),
			run: this.run.toJson(),
		};
	}

	static fromJson(raw: JsonValue): SaveGame {
		const data = jsonObj(raw);
		checkSchema(data, "save.json");
		return new SaveGame(
			jsonStr(field(data, "gameVersion")),
			jsonStr(field(data, "implementation")),
			jsonStr(field(data, "savedAt")),
			jsonInt(field(data, "rngState")),
			SessionInfo.fromJson(field(data, "session")),
			RunState.fromJson(field(data, "run")),
		);
	}
}

/** A finished run, written to history/<runId>.json. */
export interface RunRecord {
	readonly runId: string;
	readonly name: string;
	readonly vocation: string;
	readonly difficulty: string;
	readonly seed: number;
	readonly implementation: string;
	readonly gameVersion: string;
	readonly startedAt: string;
	readonly endedAt: string;
	readonly playTimeSeconds: number;
	readonly sessions: number;
	readonly round: number;
	readonly level: number;
	readonly magicLevel: number;
	readonly deathCause: string;
	readonly won: boolean;
	readonly stats: RunStatistics;
}

export function runRecordToJson(record: RunRecord): JsonObject {
	return {
		schemaVersion: SCHEMA_VERSION,
		runId: record.runId,
		name: record.name,
		vocation: record.vocation,
		difficulty: record.difficulty,
		seed: record.seed,
		implementation: record.implementation,
		gameVersion: record.gameVersion,
		startedAt: record.startedAt,
		endedAt: record.endedAt,
		playTimeSeconds: record.playTimeSeconds,
		sessions: record.sessions,
		round: record.round,
		level: record.level,
		magicLevel: record.magicLevel,
		deathCause: record.deathCause,
		won: record.won,
		stats: record.stats.toJson(),
	};
}

export function runRecordFromJson(raw: JsonValue): RunRecord {
	const data = jsonObj(raw);
	checkSchema(data, "history record");
	return {
		runId: jsonStr(field(data, "runId")),
		name: jsonStr(field(data, "name")),
		vocation: jsonStr(field(data, "vocation")),
		difficulty: jsonStr(field(data, "difficulty")),
		seed: jsonInt(field(data, "seed")),
		implementation: jsonStr(field(data, "implementation")),
		gameVersion: jsonStr(field(data, "gameVersion")),
		startedAt: jsonStr(field(data, "startedAt")),
		endedAt: jsonStr(field(data, "endedAt")),
		playTimeSeconds: jsonInt(field(data, "playTimeSeconds")),
		sessions: jsonInt(field(data, "sessions")),
		round: jsonInt(field(data, "round")),
		level: jsonInt(field(data, "level")),
		magicLevel: jsonInt(field(data, "magicLevel")),
		deathCause: jsonStr(field(data, "deathCause")),
		won: jsonBool(field(data, "won")),
		stats: RunStatistics.fromJson(field(data, "stats")),
	};
}
