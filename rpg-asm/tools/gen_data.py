"""Generates the NASM game content of the Assembly port from shared/data and shared/i18n (docs/asm.md).

The Assembly port has no JSON parser. This script (standard library only) reads the same JSON files as the other
implementations and writes two build artifacts:

- `gamedata.inc`: constants (table sizes, balance numbers as `equ`) and the `extern` declarations of the tables;
- `gamedata.asm`: the tables, filled with `istruc` against the layouts of `src/domain/definitions.inc`.

Usage: python tools/gen_data.py --shared ../shared --enums src/domain/enums.inc --out-dir build
"""

import argparse
import json
import re
from pathlib import Path
from typing import Any

ENUM_LINE = re.compile(r'^([A-Z]+)_([A-Z0-9_]+)\s+equ\s+(\d+)\s*;\s*"([^"]+)"', re.MULTILINE)
CAMEL_BOUNDARY = re.compile(r"(?<=[a-z0-9])(?=[A-Z])")

# i18n prefix of the display name of each enum group ("element.fire" → "fire"); other groups show their id.
ENUM_LABEL_PREFIX = {
	"ELEMENT": "element.",
	"SLOT": "slot.",
	"RESOURCE": "resource.",
	"STAT": "stat.",
}


def upper_snake(name: str) -> str:
	return CAMEL_BOUNDARY.sub("_", name).upper()


class Strings:
	"""Pool of NUL-terminated strings: one label per distinct text."""

	def __init__(self) -> None:
		self._labels: dict[str, str] = {}

	def label(self, text: str) -> str:
		if text not in self._labels:
			self._labels[text] = f"str_{len(self._labels)}"
		return self._labels[text]

	def lines(self) -> list[str]:
		result = []
		for text, label in self._labels.items():
			raw = text.encode("utf-8")
			if all(32 <= byte <= 126 and byte != 0x22 for byte in raw):
				body = f'"{text}", 0' if text else "0"
			else:
				# Quotes and non-ASCII bytes (UTF-8 punctuation of the translations) are emitted as numbers.
				body = ", ".join(str(byte) for byte in raw) + ", 0"
			result.append(f"{label}: db {body}")
		return result


class Enums:
	"""The enum groups declared in src/domain/enums.inc: group → [(json spelling, number)] in number order."""

	def __init__(self, source: str) -> None:
		self.groups: dict[str, list[str]] = {}
		for group, _member, number, spelling in ENUM_LINE.findall(source):
			members = self.groups.setdefault(group, [])
			if int(number) != len(members):
				raise ValueError(f"enums.inc: {group} members must be numbered 0, 1, 2... in order")
			members.append(spelling)

	def index(self, group: str, spelling: str) -> int:
		try:
			return self.groups[group].index(spelling)
		except ValueError:
			raise ValueError(f"{spelling!r} is not declared in the {group}_* enum of enums.inc") from None


class Generator:
	def __init__(self, shared: Path, enums: Enums) -> None:
		self.enums = enums
		self.strings = Strings()
		self.constants: list[tuple[str, int]] = []
		self.externs: list[str] = []
		self.out: list[str] = []
		self.data = {
			name: json.loads((shared / "data" / f"{name}.json").read_text(encoding="utf-8"))
			for name in (
				"balance",
				"monsters",
				"bosses",
				"items",
				"affixes",
				"spells",
				"statuses",
				"potions",
				"vocations",
				"families",
			)
		}
		self.i18n: dict[str, str] = json.loads((shared / "i18n" / "en.json").read_text(encoding="utf-8"))

	# ── helpers ───────────────────────────────────────────────────────────────

	def const(self, name: str, value: int) -> None:
		self.constants.append((name, value))

	def table(self, label: str, comment: str) -> None:
		self.externs.append(label)
		self.out += ["", f"; {comment}", f"global {label}", f"{label}:"]

	def row(self, struct: str, fields: dict[str, object]) -> None:
		"""One `istruc` block; `fields` must follow the declaration order of the struc."""
		self.out.append(f"\tistruc {struct}")
		for field, value in fields.items():
			text = ", ".join(str(v) for v in value) if isinstance(value, list) else str(value)
			self.out.append(f"\t\tat {struct}.{field}, dq {text}")
		self.out.append("\tiend")

	def s(self, text: str) -> str:
		return self.strings.label(text)

	def label_of(self, key: str, fallback: str) -> str:
		return self.s(self.i18n.get(key, fallback))

	@staticmethod
	def index_of(rows: list[dict[str, Any]], row_id: str, what: str) -> int:
		for index, row in enumerate(rows):
			if row["id"] == row_id:
				return index
		raise ValueError(f"unknown {what} id {row_id!r}")

	def mask(self, group: str, spellings: list[str]) -> int:
		return sum(1 << self.enums.index(group, spelling) for spelling in spellings)

	# ── tables ────────────────────────────────────────────────────────────────

	def enum_tables(self) -> None:
		for group, members in self.enums.groups.items():
			self.table(
				f"{group.lower()}_names",
				f"{group}_* → (id, display name), from src/domain/enums.inc",
			)
			prefix = ENUM_LABEL_PREFIX.get(group)
			for member in members:
				name = self.label_of(prefix + member, member) if prefix else self.s(member)
				self.row("Named", {"id": self.s(member), "name": name})

	def balance(self) -> None:
		balance = self.data["balance"]
		for key, value in balance.items():
			if isinstance(value, int):
				self.const(f"BAL_{upper_snake(key)}", value)
		for key, value in balance["caps"].items():
			self.const(f"BAL_CAP_{upper_snake(key)}", value)
		for key, value in balance["magicLevel"].items():
			self.const(f"BAL_MAGIC_LEVEL_{upper_snake(key)}", value)

		difficulties = balance["difficulties"]
		self.const("DIFFICULTY_COUNT", len(difficulties))
		self.table("difficulties", "balance.difficulties")
		for row in difficulties:
			self.row(
				"Difficulty",
				{
					"id": self.s(row["id"]),
					"name": self.label_of(f"difficulty.{row['id']}", row["id"]),
					"hp_pct": row["hpPct"],
					"damage_pct": row["damagePct"],
					"gold_pct": row["goldPct"],
					"xp_pct": row["xpPct"],
				},
			)

		rarities = balance["rarities"]
		rarity_ids = [row["id"] for row in rarities]
		self.const("RARITY_COUNT", len(rarities))
		self.const("RARITY_COMMON", rarity_ids.index("common"))
		self.const("MAX_AFFIXES", max(row["affixMax"] for row in rarities))
		self.table("rarities", "balance.rarities (the order of the weighted rarity roll)")
		for row in rarities:
			self.row(
				"Rarity",
				{
					"id": self.s(row["id"]),
					"name": self.label_of(f"rarity.{row['id']}", row["id"]),
					"stat_pct": row["statPct"],
					"value_pct": row["valuePct"],
					"affix_min": row["affixMin"],
					"affix_max": row["affixMax"],
				},
			)

		def weights(table: dict[str, int]) -> list[int]:
			unknown = set(table) - set(rarity_ids)
			if unknown:
				raise ValueError(f"rarity weights for unknown rarities: {sorted(unknown)}")
			return [table.get(rarity, 0) for rarity in rarity_ids]

		self.table("enemy_classes", "balance.enemyClasses, one row per CLASS_*")
		for class_id in self.enums.groups["CLASS"]:
			row = balance["enemyClasses"][class_id]
			self.row(
				"EnemyClass",
				{
					"id": self.s(class_id),
					"name": self.s(class_id),
					"stat_pct": row["statPct"],
					"reward_pct": row["rewardPct"],
					"dodge": row["dodge"],
					"parry": row["parry"],
					"crit": row["crit"],
					"heal": row["heal"],
					"drop_chance_pct": row["dropChancePct"],
					"drops": row["drops"],
					"potion_drop_pct": row["potionDropPct"],
					"rarity_weights": weights(row["rarityWeights"]),
				},
			)

		self.table(
			"merchant_rarity_weights",
			"balance.rarityWeights.merchant, one weight per rarity",
		)
		self.out.append(f"\tdq {', '.join(map(str, weights(balance['rarityWeights']['merchant'])))}")

		levels = balance["spellLevels"]
		self.const("SPELL_LEVEL_COUNT", len(levels))
		self.table("spell_levels", "balance.spellLevels")
		for row in levels:
			self.row(
				"SpellLevel",
				{
					"level": row["level"],
					"uses": row["uses"],
					"effect_pct": row["effectPct"],
					"mana_pct": row["manaPct"],
				},
			)

		score = balance["itemScoreWeights"]
		self.table("score_weights", "balance.itemScoreWeights, one weight per STAT_*")
		self.out.append(f"\tdq {', '.join(str(score.get(stat, 0)) for stat in self.enums.groups['STAT'])}")

		potions = self.data["potions"]["potions"]
		starting = balance["startingPotions"]
		self.const("STARTING_POTION_COUNT", len(starting))
		self.table(
			"starting_potions",
			"balance.startingPotions as (potion row, quantity) pairs",
		)
		for row in starting:
			self.out.append(f"\tdq {self.index_of(potions, row['potionId'], 'potion')}, {row['quantity']}")

	def statuses(self) -> None:
		rows = self.data["statuses"]["statuses"]
		self.const("STATUS_COUNT", len(rows))
		self.const("STATUS_STUN", self.index_of(rows, "stun", "status"))
		self.table("statuses", "statuses.json")
		for row in rows:
			self.row(
				"Status",
				{
					"id": self.s(row["id"]),
					"name": self.label_of(f"status.{row['id']}", row["id"]),
					"kind": self.enums.index("STATUSKIND", row["kind"]),
					"element": self.enums.index("ELEMENT", row["element"]),
					"turns": row["turns"],
				},
			)

	def potions(self) -> None:
		rows = self.data["potions"]["potions"]
		self.const("POTION_COUNT", len(rows))
		self.table("potions", "potions.json")
		for row in rows:
			self.row(
				"Potion",
				{
					"id": self.s(row["id"]),
					"name": self.s(row["name"]),
					"resource": self.enums.index("RESOURCE", row["resource"]),
					"min": row["min"],
					"max": row["max"],
					"price": row["price"],
					"unlock_round": row["unlockRound"],
				},
			)

	def spells(self) -> None:
		rows = self.data["spells"]["spells"]
		statuses = self.data["statuses"]["statuses"]
		self.const("SPELL_COUNT", len(rows))
		self.table("spells", "spells.json")
		for row in rows:
			bonus = row["level3Bonus"]
			status = bonus.get("status")
			self.row(
				"Spell",
				{
					"id": self.s(row["id"]),
					"name": self.s(row["name"]),
					"words": self.s(row["words"]),
					"kind": self.enums.index("SPELLKIND", row["kind"]),
					"element": self.enums.index("ELEMENT", row["element"]),
					"mana": row["mana"],
					"min": row["min"],
					"max": row["max"],
					"per_level": row["perLevel"],
					"per_magic_level": row["perMagicLevel"],
					"l3_status": -1 if status is None else self.index_of(statuses, status, "status"),
					"l3_chance": bonus.get("chance", 0),
					"l3_cleanse": int(bool(bonus.get("cleanse", False))),
				},
			)

	def items(self) -> list[str]:
		rows = self.data["items"]["items"]
		types = list(dict.fromkeys(row["type"] for row in rows))
		if len(types) > 64:
			raise ValueError("more than 64 item types: the vocation masks are one qword")
		self.const("ITEM_COUNT", len(rows))
		self.table("items", "items.json")
		for row in rows:
			element = row.get("element")
			unknown = set(row["stats"]) - set(self.enums.groups["STAT"])
			if unknown:
				raise ValueError(f"item {row['id']}: unknown stats {sorted(unknown)}")
			self.row(
				"Item",
				{
					"id": self.s(row["id"]),
					"name": self.s(row["name"]),
					"slot": self.enums.index("SLOT", row["slot"]),
					"type": types.index(row["type"]),
					"tier": row["tier"],
					"element": -1 if element is None else self.enums.index("ELEMENT", element),
					"value": row["value"],
					"stats": [row["stats"].get(stat, 0) for stat in self.enums.groups["STAT"]],
				},
			)
		# "Sorted by id" is a plain code-point comparison (docs/cross-language-parity.md §1): Python's `sorted` on
		# `str`. Sorting here keeps string comparison out of the item generator; it filters this list in order.
		self.table(
			"items_by_id",
			"item rows sorted by id (code-point order): the candidate order of the item generator",
		)
		by_id = sorted(range(len(rows)), key=lambda index: rows[index]["id"])
		self.out.append(f"\tdq {', '.join(map(str, by_id))}")
		return types

	def affixes(self) -> None:
		rows = self.data["affixes"]["affixes"]
		self.const("AFFIX_COUNT", len(rows))
		self.table("affixes", "affixes.json")
		for row in rows:
			self.row(
				"Affix",
				{
					"id": self.s(row["id"]),
					"name": self.s(row["id"]),
					"stat": self.enums.index("STAT", row["stat"]),
					"min": row["min"],
					"max": row["max"],
					"per_tier": row["perTier"],
					"slot_mask": self.mask("SLOT", row["slots"]),
				},
			)
		self.table(
			"affixes_by_id",
			"affix rows sorted by id (code-point order): the pool order of the item generator",
		)
		by_id = sorted(range(len(rows)), key=lambda index: rows[index]["id"])
		self.out.append(f"\tdq {', '.join(map(str, by_id))}")

	def vocations(self, item_types: list[str]) -> None:
		rows = self.data["vocations"]["vocations"]
		spells = self.data["spells"]["spells"]
		items = self.data["items"]["items"]
		max_spells = max(len(row["spells"]) for row in rows)
		self.const("VOCATION_COUNT", len(rows))
		self.const("MAX_VOCATION_SPELLS", max_spells)

		def type_mask(names: list[str]) -> int:
			# A type no item has can never match: it gets no bit.
			return sum(1 << item_types.index(name) for name in names if name in item_types)

		self.table("vocations", "vocations.json")
		for row in rows:
			spell_rows = [self.index_of(spells, spell, "spell") for spell in row["spells"]]
			self.row(
				"Vocation",
				{
					"id": self.s(row["id"]),
					"name": self.s(row["name"]),
					"start_hp": row["startHp"],
					"start_mp": row["startMp"],
					"hp_per_level": row["hpPerLevel"],
					"mp_per_level": row["mpPerLevel"],
					"hp_regen": row["hpRegen"],
					"mp_regen": row["mpRegen"],
					"melee_min": row["meleeMin"],
					"melee_max": row["meleeMax"],
					"melee_per_level": row["meleePerLevel"],
					"weapon_mask": type_mask(row["weaponTypes"]),
					"shield_mask": type_mask(row["shieldTypes"]),
					"starter_weapon": self.index_of(items, row["starterWeapon"], "item"),
					"spell_count": len(spell_rows),
					"spells": spell_rows + [-1] * (max_spells - len(spell_rows)),
				},
			)

	def creatures(self) -> None:
		monsters = self.data["monsters"]["monsters"]
		bosses = self.data["bosses"]["bosses"]
		statuses = self.data["statuses"]["statuses"]
		families = self.data["families"]["families"]
		everyone = [(row, False) for row in monsters] + [(row, True) for row in bosses]
		self.const("MONSTER_COUNT", len(monsters))
		self.const("BOSS_COUNT", len(bosses))
		self.const("CREATURE_COUNT", len(everyone))
		self.const("TIER_COUNT", len(bosses))
		self.const("MAX_ATTACKS", max(len(row["attacks"]) for row, _ in everyone))

		self.table("families", "families.json as (id, display name)")
		for family in families:
			self.row("Named", {"id": self.s(family), "name": self.s(family)})
		self.const("FAMILY_COUNT", len(families))

		self.out += ["", "; attacks of every creature (pointed to by Creature.attacks)"]
		for index, (row, _) in enumerate(everyone):
			self.out.append(f"creature_attacks_{index}: ; {row['id']}")
			for attack in row["attacks"]:
				status = attack.get("status")
				self.row(
					"Attack",
					{
						"id": self.s(attack["id"]),
						"element": self.enums.index("ELEMENT", attack["element"]),
						"min": attack["min"],
						"max": attack["max"],
						"weight": attack["weight"],
						"status": -1 if status is None else self.index_of(statuses, status["id"], "status"),
						"status_chance": 0 if status is None else status["chance"],
						"status_damage_pct": 0 if status is None else status["damagePct"],
					},
				)

		self.table(
			"creatures",
			"monsters.json (rows 0..MONSTER_COUNT-1) followed by bosses.json",
		)
		for index, (row, is_boss) in enumerate(everyone):
			if row["family"] not in families:
				raise ValueError(f"creature {row['id']}: unknown family {row['family']!r}")
			charge = row.get("chargeAttack")
			resist = row["resistances"]
			for element in resist:
				self.enums.index("ELEMENT", element)
			self.row(
				"Creature",
				{
					"id": self.s(row["id"]),
					"name": self.s(row["name"]),
					"family": self.s(row["family"]),
					"tier": row["tier"],
					"hp": row["hp"],
					"xp": row["xp"],
					"gold_min": row["gold"]["min"],
					"gold_max": row["gold"]["max"],
					"is_boss": int(is_boss),
					"charge_attack": -1 if charge is None else self.index_of(row["attacks"], charge, "attack"),
					"attack_count": len(row["attacks"]),
					"attacks": f"creature_attacks_{index}",
					"resist": [resist.get(element, 100) for element in self.enums.groups["ELEMENT"]],
				},
			)

		tiers = len(bosses)
		by_tier = [
			sorted(
				(i for i, row in enumerate(monsters) if row["tier"] == tier),
				key=lambda i: monsters[i]["id"],
			)
			for tier in range(tiers)
		]
		self.table(
			"tier_monsters",
			"monster rows of every tier, each tier sorted by id (code-point order)",
		)
		for tier, group in enumerate(by_tier):
			self.out.append(f"\tdq {', '.join(map(str, group))} ; tier {tier}")
		self.table(
			"tier_monster_start",
			"index in tier_monsters of the first monster of each tier",
		)
		starts = [sum(len(group) for group in by_tier[:tier]) for tier in range(tiers)]
		self.out.append(f"\tdq {', '.join(map(str, starts))}")
		self.table("tier_monster_count", "number of monsters in each tier")
		self.out.append(f"\tdq {', '.join(str(len(group)) for group in by_tier)}")
		boss_of_tier = {row["tier"]: len(monsters) + i for i, row in enumerate(bosses)}
		self.table("tier_boss", "creature row of the boss of each tier")
		self.out.append(f"\tdq {', '.join(str(boss_of_tier[tier]) for tier in range(tiers))}")

	def translations(self) -> None:
		self.const("I18N_COUNT", len(self.i18n))
		self.table(
			"i18n_table",
			"shared/i18n/en.json as (key, text) pairs (see infrastructure/i18n.asm)",
		)
		for key, text in self.i18n.items():
			self.out.append(f"\tdq {self.s(key)}, {self.s(text)}")

	# ── output ────────────────────────────────────────────────────────────────

	def run(self) -> tuple[str, str]:
		self.enum_tables()
		self.balance()
		self.statuses()
		self.potions()
		self.spells()
		item_types = self.items()
		self.affixes()
		self.vocations(item_types)
		self.creatures()
		self.translations()

		banner = "; GENERATED by tools/gen_data.py from shared/data/*.json and shared/i18n/en.json — do not edit."
		inc = [banner, "%ifndef GAMEDATA_INC", "%define GAMEDATA_INC", ""]
		inc += [f"{name:<32} equ {value}" for name, value in self.constants]
		inc += ["", "%ifndef GAMEDATA_IMPL"]
		inc += [f"extern {label}" for label in self.externs]
		inc += ["%endif", "", "%endif", ""]

		asm = [
			banner,
			"%define GAMEDATA_IMPL",
			'%include "domain/enums.inc"',
			'%include "gamedata.inc"',
			'%include "domain/definitions.inc"',
			"",
			"section .note.GNU-stack noalloc noexec nowrite progbits",
			"section .rodata align=8",
			*self.out,
			"",
			"; string pool",
			*self.strings.lines(),
			"",
		]
		return "\n".join(inc), "\n".join(asm)


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--shared", type=Path, required=True, help="path of the shared/ folder")
	parser.add_argument("--enums", type=Path, required=True, help="path of src/domain/enums.inc")
	parser.add_argument("--out-dir", type=Path, required=True)
	args = parser.parse_args()

	generator = Generator(args.shared, Enums(args.enums.read_text(encoding="utf-8")))
	inc, asm = generator.run()
	args.out_dir.mkdir(parents=True, exist_ok=True)
	(args.out_dir / "gamedata.inc").write_text(inc, encoding="utf-8", newline="\n")
	(args.out_dir / "gamedata.asm").write_text(asm, encoding="utf-8", newline="\n")


if __name__ == "__main__":
	main()
