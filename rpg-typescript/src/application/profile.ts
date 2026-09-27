/** Cross-run profile: bestiary, achievements and Hall of Fame (docs/game-design.md §11). */
import type { AchievementDef, GameData } from "../domain/definitions";
import { field, type JsonObject, type JsonValue, jsonInt, jsonList, jsonObj, jsonStr } from "../domain/json-types";
import type { Event } from "./events";
import type { RunState } from "./run-state";

const PROFILE_SCHEMA_VERSION = 1;
export const HALL_OF_FAME_SIZE = 10;
export const BESTIARY_REVEAL_KILLS = 5;

export interface BestiaryEntry {
	kills: number;
	readonly firstKilledAt: string;
}

export interface Unlock {
	readonly unlockedAt: string;
	readonly runId: string;
}

export interface HallOfFameEntry {
	readonly runId: string;
	readonly name: string;
	readonly vocation: string;
	readonly difficulty: string;
	readonly round: number;
	readonly level: number;
	readonly endedAt: string;
}

function hallEntryToJson(entry: HallOfFameEntry): JsonObject {
	return {
		runId: entry.runId,
		name: entry.name,
		vocation: entry.vocation,
		difficulty: entry.difficulty,
		round: entry.round,
		level: entry.level,
		endedAt: entry.endedAt,
	};
}

function hallEntryFromJson(raw: JsonValue): HallOfFameEntry {
	const data = jsonObj(raw);
	return {
		runId: jsonStr(field(data, "runId")),
		name: jsonStr(field(data, "name")),
		vocation: jsonStr(field(data, "vocation")),
		difficulty: jsonStr(field(data, "difficulty")),
		round: jsonInt(field(data, "round")),
		level: jsonInt(field(data, "level")),
		endedAt: jsonStr(field(data, "endedAt")),
	};
}

export class Profile {
	constructor(
		public bestiary = new Map<string, BestiaryEntry>(),
		public achievements = new Map<string, Unlock>(),
		public hallOfFame: HallOfFameEntry[] = [],
	) {}

	toJson(): JsonObject {
		const bestiary: JsonObject = {};
		for (const key of [...this.bestiary.keys()].sort()) {
			const entry = this.bestiary.get(key);
			if (entry !== undefined) bestiary[key] = { kills: entry.kills, firstKilledAt: entry.firstKilledAt };
		}
		const achievements: JsonObject = {};
		for (const key of [...this.achievements.keys()].sort()) {
			const unlock = this.achievements.get(key);
			if (unlock !== undefined) achievements[key] = { unlockedAt: unlock.unlockedAt, runId: unlock.runId };
		}
		return {
			schemaVersion: PROFILE_SCHEMA_VERSION,
			bestiary,
			achievements,
			hallOfFame: this.hallOfFame.map(hallEntryToJson),
		};
	}

	static fromJson(raw: JsonValue): Profile {
		const data = jsonObj(raw);
		const bestiary = new Map<string, BestiaryEntry>();
		for (const [key, value] of Object.entries(jsonObj(field(data, "bestiary")))) {
			const entry = jsonObj(value);
			bestiary.set(key, {
				kills: jsonInt(field(entry, "kills")),
				firstKilledAt: jsonStr(field(entry, "firstKilledAt")),
			});
		}
		const achievements = new Map<string, Unlock>();
		for (const [key, value] of Object.entries(jsonObj(field(data, "achievements")))) {
			const unlock = jsonObj(value);
			achievements.set(key, {
				unlockedAt: jsonStr(field(unlock, "unlockedAt")),
				runId: jsonStr(field(unlock, "runId")),
			});
		}
		return new Profile(bestiary, achievements, jsonList(field(data, "hallOfFame")).map(hallEntryFromJson));
	}
}

function compareHallOfFame(a: HallOfFameEntry, b: HallOfFameEntry): number {
	if (a.round !== b.round) return b.round - a.round;
	if (a.level !== b.level) return b.level - a.level;
	return a.endedAt < b.endedAt ? -1 : a.endedAt > b.endedAt ? 1 : 0;
}

/** Feeds the profile from engine events. Returns the achievements unlocked by each step. */
export class ProfileService {
	constructor(
		private readonly data: GameData,
		public profile: Profile,
	) {}

	observe(events: readonly Event[], state: RunState, now: string, runId: string): AchievementDef[] {
		for (const evt of events) {
			if (evt.type !== "monster_killed") continue;
			const monsterId = String(evt.monsterId);
			const entry = this.profile.bestiary.get(monsterId);
			if (entry === undefined) this.profile.bestiary.set(monsterId, { kills: 1, firstKilledAt: now });
			else entry.kills += 1;
		}
		const unlocked: AchievementDef[] = [];
		for (const achievement of this.data.achievements) {
			if (this.profile.achievements.has(achievement.id)) continue;
			if (this.progress(achievement, state) >= achievement.value) {
				this.profile.achievements.set(achievement.id, { unlockedAt: now, runId });
				unlocked.push(achievement);
			}
		}
		return unlocked;
	}

	private progress(achievement: AchievementDef, state: RunState): number {
		const player = state.player;
		switch (achievement.type) {
			case "kills_total":
				return [...this.profile.bestiary.values()].reduce((sum, entry) => sum + entry.kills, 0);
			case "bosses_total": {
				const bossIds = new Set(this.data.bosses.map((boss) => boss.id));
				return [...this.profile.bestiary.entries()]
					.filter(([key]) => bossIds.has(key))
					.reduce((sum, [, entry]) => sum + entry.kills, 0);
			}
			case "round_reached":
				return state.round;
			case "level_reached":
				return player.level;
			case "legendary_found":
				return state.stats.itemsDropped.get("legendary");
			case "spell_level_3": {
				const levels = this.data.balance.spellLevels;
				const threshold = levels[levels.length - 1]?.uses ?? Number.POSITIVE_INFINITY;
				return [...player.spellUses.values()].filter((uses) => uses >= threshold).length;
			}
			case "gold_held":
				return player.gold;
			case "distinct_monsters":
				return this.profile.bestiary.size;
			case "hard_round_reached":
				return state.config.difficultyId === "hard" ? state.round : 0;
			default:
				return 0;
		}
	}

	recordFinishedRun(entry: HallOfFameEntry): void {
		this.profile.hallOfFame = [...this.profile.hallOfFame, entry]
			.sort(compareHallOfFame)
			.slice(0, HALL_OF_FAME_SIZE);
	}

	revealed(monsterId: string): boolean {
		const entry = this.profile.bestiary.get(monsterId);
		return entry !== undefined && entry.kills >= BESTIARY_REVEAL_KILLS;
	}
}
