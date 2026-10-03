"""Loads shared/data/*.json into immutable domain definitions."""

import json
from pathlib import Path

from rpg.domain.definitions import (
	AchievementDef,
	AffixDef,
	AutoBattleDef,
	AutoBattleModeDef,
	Balance,
	Caps,
	DifficultyDef,
	EnemyClassDef,
	GameData,
	ItemDef,
	Level3Bonus,
	MonsterAttack,
	MonsterDef,
	PotionDef,
	RarityDef,
	SpellDef,
	SpellLevelDef,
	StatusDef,
	StatusOnHit,
	VocationDef,
)
from rpg.domain.enums import Element, EnemyClass, Resource, Slot, SpellKind, Stat, StatusKind
from rpg.domain.json_types import JsonObject, JsonValue, json_bool, json_int, json_list, json_obj, json_str


class DataError(ValueError):
	"""Raised when a data file is missing a field or has an invalid value."""


def _read(directory: Path, name: str) -> JsonObject:
	path = directory / f"{name}.json"
	try:
		return json_obj(json.loads(path.read_text(encoding="utf-8")))
	except (OSError, json.JSONDecodeError, TypeError) as exc:
		raise DataError(f"{path}: {exc}") from exc


def _objects(document: JsonObject, key: str) -> list[JsonObject]:
	return [json_obj(entry) for entry in json_list(document[key])]


def _strs(value: JsonValue) -> tuple[str, ...]:
	return tuple(json_str(v) for v in json_list(value))


def _gold(raw: JsonValue) -> tuple[int, int]:
	gold = json_obj(raw)
	return json_int(gold["min"]), json_int(gold["max"])


def _attack(raw: JsonObject) -> MonsterAttack:
	status_raw = raw.get("status")
	status = None
	if status_raw is not None:
		status_obj = json_obj(status_raw)
		status = StatusOnHit(
			json_str(status_obj["id"]), json_int(status_obj["chance"]), json_int(status_obj["damagePct"])
		)
	return MonsterAttack(
		id=json_str(raw["id"]),
		element=Element(json_str(raw["element"])),
		min=json_int(raw["min"]),
		max=json_int(raw["max"]),
		weight=json_int(raw["weight"]),
		status=status,
	)


def _creature(raw: JsonObject, is_boss: bool) -> MonsterDef:
	gold_min, gold_max = _gold(raw["gold"])
	charge = raw.get("chargeAttack")
	return MonsterDef(
		id=json_str(raw["id"]),
		name=json_str(raw["name"]),
		tier=json_int(raw["tier"]),
		family=json_str(raw["family"]),
		hp=json_int(raw["hp"]),
		xp=json_int(raw["xp"]),
		gold_min=gold_min,
		gold_max=gold_max,
		attacks=tuple(_attack(json_obj(a)) for a in json_list(raw["attacks"])),
		resistances={Element(k): json_int(v) for k, v in json_obj(raw["resistances"]).items()},
		is_boss=is_boss,
		charge_attack=None if charge is None else json_str(charge),
	)


def _spell(raw: JsonObject) -> SpellDef:
	bonus = json_obj(raw["level3Bonus"])
	status = bonus.get("status")
	return SpellDef(
		id=json_str(raw["id"]),
		name=json_str(raw["name"]),
		words=json_str(raw["words"]),
		kind=SpellKind(json_str(raw["kind"])),
		element=Element(json_str(raw["element"])),
		mana=json_int(raw["mana"]),
		min=json_int(raw["min"]),
		max=json_int(raw["max"]),
		per_level=json_int(raw["perLevel"]),
		per_magic_level=json_int(raw["perMagicLevel"]),
		level3_bonus=Level3Bonus(
			status=None if status is None else json_str(status),
			chance=json_int(bonus.get("chance", 0)),
			cleanse=json_bool(bonus.get("cleanse", False)),
		),
	)


def _vocation(raw: JsonObject) -> VocationDef:
	return VocationDef(
		id=json_str(raw["id"]),
		name=json_str(raw["name"]),
		start_hp=json_int(raw["startHp"]),
		start_mp=json_int(raw["startMp"]),
		hp_per_level=json_int(raw["hpPerLevel"]),
		mp_per_level=json_int(raw["mpPerLevel"]),
		hp_regen=json_int(raw["hpRegen"]),
		mp_regen=json_int(raw["mpRegen"]),
		melee_min=json_int(raw["meleeMin"]),
		melee_max=json_int(raw["meleeMax"]),
		melee_per_level=json_int(raw["meleePerLevel"]),
		weapon_types=_strs(raw["weaponTypes"]),
		shield_types=_strs(raw["shieldTypes"]),
		starter_weapon=json_str(raw["starterWeapon"]),
		spells=_strs(raw["spells"]),
	)


def _weights(raw: JsonValue) -> dict[str, int]:
	return {rarity: json_int(weight) for rarity, weight in json_obj(raw).items()}


def _enemy_class(class_id: str, raw: JsonObject) -> EnemyClassDef:
	return EnemyClassDef(
		id=class_id,
		stat_pct=json_int(raw["statPct"]),
		reward_pct=json_int(raw["rewardPct"]),
		dodge=json_int(raw["dodge"]),
		parry=json_int(raw["parry"]),
		crit=json_int(raw["crit"]),
		heal=json_int(raw["heal"]),
		drop_chance_pct=json_int(raw["dropChancePct"]),
		drops=json_int(raw["drops"]),
		potion_drop_pct=json_int(raw["potionDropPct"]),
		rarity_weights=_weights(raw["rarityWeights"]),
	)


def _auto_battle(raw: JsonObject) -> AutoBattleDef:
	modes = json_obj(raw["modes"])
	return AutoBattleDef(
		heal_below_pct=json_int(raw["healBelowPct"]),
		mana_below_pct=json_int(raw["manaBelowPct"]),
		emergency_heal_below_pct=json_int(raw["emergencyHealBelowPct"]),
		modes=tuple(
			AutoBattleModeDef(
				id=mode_id,
				offense=json_str(json_obj(mode)["offense"]),
				support_every=json_int(json_obj(mode)["supportEvery"]),
			)
			for mode_id, mode in modes.items()
		),
	)


def _balance(raw: JsonObject) -> Balance:
	caps = json_obj(raw["caps"])
	magic = json_obj(raw["magicLevel"])
	classes = json_obj(raw["enemyClasses"])
	return Balance(
		rounds_per_tier=json_int(raw["roundsPerTier"]),
		cycle_stat_pct=json_int(raw["cycleStatPct"]),
		cycle_reward_pct=json_int(raw["cycleRewardPct"]),
		position_pct=json_int(raw["positionPct"]),
		final_round=json_int(raw["finalRound"]),
		elite_chance_pct=json_int(raw["eliteChancePct"]),
		difficulties=tuple(
			DifficultyDef(
				id=json_str(d["id"]),
				hp_pct=json_int(d["hpPct"]),
				damage_pct=json_int(d["damagePct"]),
				gold_pct=json_int(d["goldPct"]),
				xp_pct=json_int(d["xpPct"]),
			)
			for d in _objects(raw, "difficulties")
		),
		enemy_classes=tuple(_enemy_class(c.value, json_obj(classes[c.value])) for c in EnemyClass),
		crit_multiplier_pct=json_int(raw["critMultiplierPct"]),
		defend_damage_pct=json_int(raw["defendDamagePct"]),
		parry_reflect_pct=json_int(raw["parryReflectPct"]),
		monster_heal_pct=json_int(raw["monsterHealPct"]),
		boss_telegraph_every=json_int(raw["bossTelegraphEvery"]),
		boss_charge_damage_pct=json_int(raw["bossChargeDamagePct"]),
		caps=Caps(
			crit_chance=json_int(caps["critChance"]),
			dodge=json_int(caps["dodge"]),
			parry=json_int(caps["parry"]),
			leech=json_int(caps["leech"]),
			protection=json_int(caps["protection"]),
		),
		magic_level_base=json_int(magic["base"]),
		magic_level_growth_pct=json_int(magic["growthPct"]),
		spell_levels=tuple(
			SpellLevelDef(
				level=json_int(s["level"]),
				uses=json_int(s["uses"]),
				effect_pct=json_int(s["effectPct"]),
				mana_pct=json_int(s["manaPct"]),
			)
			for s in _objects(raw, "spellLevels")
		),
		starting_gold=json_int(raw["startingGold"]),
		starting_potions=tuple(
			(json_str(p["potionId"]), json_int(p["quantity"])) for p in _objects(raw, "startingPotions")
		),
		bag_capacity=json_int(raw["bagCapacity"]),
		item_level_per_tier=json_int(raw["itemLevelPerTier"]),
		item_score_weights={Stat(k): json_int(v) for k, v in json_obj(raw["itemScoreWeights"]).items()},
		rarities=tuple(
			RarityDef(
				id=json_str(r["id"]),
				stat_pct=json_int(r["statPct"]),
				value_pct=json_int(r["valuePct"]),
				affix_min=json_int(r["affixMin"]),
				affix_max=json_int(r["affixMax"]),
			)
			for r in _objects(raw, "rarities")
		),
		rarity_weights={table: _weights(weights) for table, weights in json_obj(raw["rarityWeights"]).items()},
		merchant_stock_size=json_int(raw["merchantStockSize"]),
		merchant_markup_pct=json_int(raw["merchantMarkupPct"]),
		spell_status_damage_pct=json_int(raw["spellStatusDamagePct"]),
		auto_battle=_auto_battle(json_obj(raw["autoBattle"])),
	)


def _item(raw: JsonObject) -> ItemDef:
	element = raw.get("element")
	return ItemDef(
		id=json_str(raw["id"]),
		name=json_str(raw["name"]),
		slot=Slot(json_str(raw["slot"])),
		type=json_str(raw["type"]),
		tier=json_int(raw["tier"]),
		element=None if element is None else Element(json_str(element)),
		stats={Stat(k): json_int(v) for k, v in json_obj(raw["stats"]).items()},
		value=json_int(raw["value"]),
	)


def _optional(directory: Path, name: str) -> JsonObject:
	return _read(directory, name) if (directory / f"{name}.json").exists() else {name: []}


def load_game_data(shared_dir: Path) -> GameData:
	directory = shared_dir / "data"
	try:
		affixes = _optional(directory, "affixes")
		achievements = _optional(directory, "achievements")
		return GameData(
			balance=_balance(_read(directory, "balance")),
			vocations=tuple(_vocation(v) for v in _objects(_read(directory, "vocations"), "vocations")),
			spells=tuple(_spell(s) for s in _objects(_read(directory, "spells"), "spells")),
			monsters=tuple(_creature(m, False) for m in _objects(_read(directory, "monsters"), "monsters")),
			bosses=tuple(_creature(b, True) for b in _objects(_read(directory, "bosses"), "bosses")),
			potions=tuple(
				PotionDef(
					id=json_str(p["id"]),
					name=json_str(p["name"]),
					resource=Resource(json_str(p["resource"])),
					min=json_int(p["min"]),
					max=json_int(p["max"]),
					price=json_int(p["price"]),
					unlock_round=json_int(p["unlockRound"]),
				)
				for p in _objects(_read(directory, "potions"), "potions")
			),
			statuses=tuple(
				StatusDef(
					id=json_str(s["id"]),
					kind=StatusKind(json_str(s["kind"])),
					element=Element(json_str(s["element"])),
					turns=json_int(s["turns"]),
				)
				for s in _objects(_read(directory, "statuses"), "statuses")
			),
			items=tuple(_item(i) for i in _objects(_read(directory, "items"), "items")),
			affixes=tuple(
				AffixDef(
					id=json_str(a["id"]),
					stat=Stat(json_str(a["stat"])),
					min=json_int(a["min"]),
					max=json_int(a["max"]),
					per_tier=json_int(a["perTier"]),
					slots=tuple(Slot(s) for s in _strs(a["slots"])),
				)
				for a in _objects(affixes, "affixes")
			),
			achievements=tuple(
				AchievementDef(id=json_str(a["id"]), type=json_str(a["type"]), value=json_int(a["value"]))
				for a in _objects(achievements, "achievements")
			),
			families=_strs(_read(directory, "families")["families"]),
		)
	except (KeyError, TypeError, ValueError) as exc:
		if isinstance(exc, DataError):
			raise
		raise DataError(f"invalid game data in {directory}: {exc!r}") from exc
