/**
 * Upgrades older save/settings/history/profile documents to the current schema (docs/persistence.md).
 *
 * Each migration takes the raw JSON of version N and returns version N + 1, so the application layer only ever reads
 * the current format. Version 1 → 2 is the 1.4.0 "ARPG update".
 */
import { type JsonObject, type JsonValue, jsonInt, jsonObj } from "../domain/json-types";

const REMOVED_RARITY = "epic";
const REPLACEMENT_RARITY = "legendary";

function version(document: JsonObject): number {
	return jsonInt(document.schemaVersion ?? 1);
}

function isObject(value: JsonValue | undefined): value is JsonObject {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function setDefault(document: JsonObject, key: string, value: JsonValue): void {
	if (!(key in document)) document[key] = value;
}

function renameRarities(items: readonly JsonValue[]): void {
	for (const raw of items) {
		const item = jsonObj(raw);
		if (item.rarity === REMOVED_RARITY) item.rarity = REPLACEMENT_RARITY;
	}
}

function statsV1ToV2(raw: JsonValue | undefined): void {
	const stats = jsonObj(raw ?? null);
	setDefault(stats, "itemsAutoEquipped", 0);
	setDefault(stats, "elitesKilled", 0);
	setDefault(stats, "potionsDropped", {});
	const dropped = jsonObj(stats.itemsDropped ?? null);
	if (REMOVED_RARITY in dropped) {
		const epic = jsonInt(dropped[REMOVED_RARITY] ?? 0);
		delete dropped[REMOVED_RARITY];
		dropped[REPLACEMENT_RARITY] = jsonInt(dropped[REPLACEMENT_RARITY] ?? 0) + epic;
		const sorted: JsonObject = {};
		for (const key of Object.keys(dropped).sort()) sorted[key] = dropped[key] ?? 0;
		stats.itemsDropped = sorted;
	}
	if (Array.isArray(stats.droppedItems)) renameRarities(stats.droppedItems);
}

function runV1ToV2(raw: JsonValue | undefined): void {
	const run = jsonObj(raw ?? null);
	setDefault(jsonObj(run.config ?? null), "autoEquip", false);
	setDefault(run, "won", false);
	const monster = run.monster;
	if (isObject(monster)) setDefault(monster, "enemyClass", monster.isBoss === true ? "boss" : "normal");
	const player = jsonObj(run.player ?? null);
	if (Array.isArray(player.bag)) renameRarities(player.bag);
	const equipment = player.equipment;
	if (isObject(equipment)) renameRarities(Object.values(equipment));
	if (Array.isArray(run.merchantStock)) renameRarities(run.merchantStock);
	statsV1ToV2(run.stats);
}

export function migrateSave(document: JsonObject): JsonObject {
	if (version(document) < 2) {
		runV1ToV2(document.run);
		document.schemaVersion = 2;
	}
	return document;
}

export function migrateHistory(document: JsonObject): JsonObject {
	if (version(document) < 2) {
		setDefault(document, "won", false);
		statsV1ToV2(document.stats);
		document.schemaVersion = 2;
	}
	return document;
}

export function migrateProfile(document: JsonObject): JsonObject {
	if (version(document) < 2) {
		const hall = document.hallOfFame;
		if (Array.isArray(hall)) {
			for (const entry of hall) setDefault(jsonObj(entry), "won", false);
		}
		document.schemaVersion = 2;
	}
	return document;
}

export function migrateSettings(document: JsonObject): JsonObject {
	if (version(document) < 2) {
		setDefault(document, "autoEquip", false);
		setDefault(document, "battleSpeed", 1);
		document.schemaVersion = 2;
	}
	return document;
}
