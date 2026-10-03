//! Derived character stats: vocation base + equipment (docs/game-design.md §4 and §8).

use std::collections::BTreeMap;

use crate::domain::definitions::GameData;
use crate::domain::entities::{ItemInstance, Player};
use crate::domain::enums::{ELEMENTS, Element, Slot, Stat, protection_by_element};
use crate::domain::formulas::pct;

pub fn item_stats(item: &ItemInstance, data: &GameData) -> BTreeMap<Stat, i64> {
	let definition = data.item(&item.item_id);
	let rarity = data.balance.rarity(&item.rarity);
	let mut stats: BTreeMap<Stat, i64> =
		definition.stats.iter().map(|(&stat, &value)| (stat, pct(value, rarity.stat_pct))).collect();
	for affix in &item.affixes {
		*stats.entry(affix.stat).or_insert(0) += affix.value;
	}
	stats
}

pub fn item_value(item: &ItemInstance, data: &GameData) -> i64 {
	pct(data.item(&item.item_id).value, data.balance.rarity(&item.rarity).value_pct)
}

/// Sum of the item's final stats weighted by `balance.itemScoreWeights` (like Diablo's item power).
pub fn item_score(item: &ItemInstance, data: &GameData) -> i64 {
	let weights = &data.balance.item_score_weights;
	item_stats(item, data).iter().map(|(stat, value)| value * weights.get(stat).copied().unwrap_or(0)).sum()
}

/// Uses the instance tier: the round tier the item was generated for (docs/game-design.md §8).
pub fn required_level(item: &ItemInstance, data: &GameData) -> i64 {
	1 + item.tier * data.balance.item_level_per_tier
}

pub fn equipment_score(player: &Player, data: &GameData) -> i64 {
	player.equipment.values().map(|item| item_score(item, data)).sum()
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CharacterSheet {
	pub max_hp: i64,
	pub max_mp: i64,
	pub hp_regen: i64,
	pub mp_regen: i64,
	pub melee_min: i64,
	pub melee_max: i64,
	pub weapon_element: Element,
	pub armor: i64,
	pub crit_chance: i64,
	pub crit_damage: i64,
	pub spell_power: i64,
	pub physical_damage: i64,
	pub dodge: i64,
	pub parry: i64,
	pub life_leech: i64,
	pub mana_leech: i64,
	/// Every element in declaration order (the character screen lists them in this order).
	pub protections: BTreeMap<Element, i64>,
}

impl CharacterSheet {
	pub fn protection(&self, element: Element) -> i64 {
		self.protections.get(&element).copied().unwrap_or(0)
	}
}

pub fn build_sheet(player: &Player, data: &GameData) -> CharacterSheet {
	let vocation = data.vocation(&player.vocation_id);
	let caps = &data.balance.caps;
	let mut totals: BTreeMap<Stat, i64> = BTreeMap::new();
	for item in player.equipment.values() {
		for (stat, value) in item_stats(item, data) {
			*totals.entry(stat).or_insert(0) += value;
		}
	}
	let total = |stat: Stat| totals.get(&stat).copied().unwrap_or(0);

	let weapon_element = player
		.equipment
		.get(&Slot::Weapon)
		.and_then(|weapon| data.item(&weapon.item_id).element)
		.unwrap_or(Element::Physical);

	let level_bonus = (player.level - 1) * vocation.melee_per_level;
	let attack = total(Stat::Attack);
	CharacterSheet {
		max_hp: vocation.start_hp + (player.level - 1) * vocation.hp_per_level + total(Stat::MaxHp),
		max_mp: vocation.start_mp + (player.level - 1) * vocation.mp_per_level + total(Stat::MaxMp),
		hp_regen: vocation.hp_regen + total(Stat::HpRegen),
		mp_regen: vocation.mp_regen + total(Stat::MpRegen),
		melee_min: vocation.melee_min + level_bonus + attack,
		melee_max: vocation.melee_max + level_bonus + attack,
		weapon_element,
		armor: total(Stat::Armor),
		crit_chance: total(Stat::CritChance).min(caps.crit_chance),
		crit_damage: total(Stat::CritDamage),
		spell_power: total(Stat::SpellPower),
		physical_damage: total(Stat::PhysicalDamage),
		dodge: total(Stat::Dodge).min(caps.dodge),
		parry: total(Stat::Parry).min(caps.parry),
		life_leech: total(Stat::LifeLeech).min(caps.leech),
		mana_leech: total(Stat::ManaLeech).min(caps.leech),
		protections: ELEMENTS
			.iter()
			.map(|&element| (element, total(protection_by_element(element)).min(caps.protection)))
			.collect(),
	}
}
