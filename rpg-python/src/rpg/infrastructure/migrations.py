"""Upgrades older save/settings/history/profile documents to the current schema (docs/persistence.md).

Each migration takes the raw JSON of version N and returns version N + 1, so the application layer only ever reads
the current format. Version 1 → 2 is the 1.4.0 "ARPG update".
"""

from rpg.domain.enums import EnemyClass
from rpg.domain.json_types import JsonObject, JsonValue, json_int, json_obj

REMOVED_RARITY = "epic"
REPLACEMENT_RARITY = "legendary"


def _version(document: JsonObject) -> int:
	return json_int(document.get("schemaVersion", 1))


def _rename_rarities(items: JsonValue) -> None:
	if not isinstance(items, list):
		return
	for raw in items:
		item = json_obj(raw)
		if item.get("rarity") == REMOVED_RARITY:
			item["rarity"] = REPLACEMENT_RARITY


def _stats_v1_to_v2(raw: JsonValue) -> None:
	stats = json_obj(raw)
	stats.setdefault("itemsAutoEquipped", 0)
	stats.setdefault("elitesKilled", 0)
	stats.setdefault("potionsDropped", {})
	dropped = json_obj(stats["itemsDropped"])
	if REMOVED_RARITY in dropped:
		epic = json_int(dropped.pop(REMOVED_RARITY))
		dropped[REPLACEMENT_RARITY] = json_int(dropped.get(REPLACEMENT_RARITY, 0)) + epic
		stats["itemsDropped"] = dict(sorted(dropped.items()))
	_rename_rarities(stats.get("droppedItems"))


def _run_v1_to_v2(raw: JsonValue) -> None:
	run = json_obj(raw)
	json_obj(run["config"]).setdefault("autoEquip", False)
	run.setdefault("won", False)
	monster = run.get("monster")
	if isinstance(monster, dict):
		monster.setdefault(
			"enemyClass", (EnemyClass.BOSS if monster.get("isBoss") is True else EnemyClass.NORMAL).value
		)
	player = json_obj(run["player"])
	_rename_rarities(player.get("bag"))
	equipment = player.get("equipment")
	if isinstance(equipment, dict):
		_rename_rarities(list(equipment.values()))
	_rename_rarities(run.get("merchantStock"))
	_stats_v1_to_v2(run["stats"])


def migrate_save(document: JsonObject) -> JsonObject:
	if _version(document) < 2:
		_run_v1_to_v2(document["run"])
		document["schemaVersion"] = 2
	return document


def migrate_history(document: JsonObject) -> JsonObject:
	if _version(document) < 2:
		document.setdefault("won", False)
		_stats_v1_to_v2(document["stats"])
		document["schemaVersion"] = 2
	return document


def migrate_profile(document: JsonObject) -> JsonObject:
	if _version(document) < 2:
		hall = document.get("hallOfFame")
		if isinstance(hall, list):
			for entry in hall:
				json_obj(entry).setdefault("won", False)
		document["schemaVersion"] = 2
	return document


def migrate_settings(document: JsonObject) -> JsonObject:
	if _version(document) < 2:
		document.setdefault("autoEquip", False)
		document.setdefault("battleSpeed", 1)
		document["schemaVersion"] = 2
	return document
