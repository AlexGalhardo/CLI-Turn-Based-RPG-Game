//! Immutable game definitions loaded from shared/data (see docs/data-format.md).
//!
//! The structs derive `Deserialize` with the JSON field names, so the loader is mostly declarative.

use std::collections::{BTreeMap, HashMap};

use serde::{Deserialize, Serialize};

use crate::domain::enums::{Element, Resource, Slot, SpellKind, Stat, StatusKind};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusOnHit {
	#[serde(rename = "id")]
	pub status: String,
	pub chance: i64,
	pub damage_pct: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MonsterAttack {
	pub id: String,
	pub element: Element,
	pub min: i64,
	pub max: i64,
	pub weight: i64,
	#[serde(default, skip_serializing_if = "Option::is_none")]
	pub status: Option<StatusOnHit>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
pub struct GoldRange {
	pub min: i64,
	pub max: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MonsterDef {
	pub id: String,
	pub name: String,
	pub tier: i64,
	pub family: String,
	pub hp: i64,
	pub xp: i64,
	pub gold: GoldRange,
	pub attacks: Vec<MonsterAttack>,
	pub resistances: BTreeMap<Element, i64>,
	/// Not in the JSON: set by the loader for entries of `bosses.json`.
	#[serde(skip)]
	pub is_boss: bool,
	#[serde(default)]
	pub charge_attack: Option<String>,
}

impl MonsterDef {
	/// Damage taken in percent (100 when the element is not listed).
	pub fn resistance(&self, element: Element) -> i64 {
		self.resistances.get(&element).copied().unwrap_or(100)
	}

	pub fn find_attack(&self, attack_id: &str) -> Option<&MonsterAttack> {
		self.attacks.iter().find(|attack| attack.id == attack_id)
	}

	pub fn attack(&self, attack_id: &str) -> &MonsterAttack {
		self.find_attack(attack_id).unwrap_or_else(|| unknown_id(attack_id))
	}
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
pub struct Level3Bonus {
	#[serde(default)]
	pub status: Option<String>,
	#[serde(default)]
	pub chance: i64,
	#[serde(default)]
	pub cleanse: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpellDef {
	pub id: String,
	pub name: String,
	pub words: String,
	pub kind: SpellKind,
	pub element: Element,
	pub mana: i64,
	pub min: i64,
	pub max: i64,
	pub per_level: i64,
	pub per_magic_level: i64,
	pub level3_bonus: Level3Bonus,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct VocationDef {
	pub id: String,
	pub name: String,
	pub start_hp: i64,
	pub start_mp: i64,
	pub hp_per_level: i64,
	pub mp_per_level: i64,
	pub hp_regen: i64,
	pub mp_regen: i64,
	pub melee_min: i64,
	pub melee_max: i64,
	pub melee_per_level: i64,
	pub weapon_types: Vec<String>,
	pub shield_types: Vec<String>,
	pub starter_weapon: String,
	pub spells: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PotionDef {
	pub id: String,
	pub name: String,
	pub resource: Resource,
	pub min: i64,
	pub max: i64,
	pub price: i64,
	pub unlock_round: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct StatusDef {
	pub id: String,
	pub kind: StatusKind,
	pub element: Element,
	pub turns: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct ItemDef {
	pub id: String,
	pub name: String,
	pub slot: Slot,
	#[serde(rename = "type")]
	pub item_type: String,
	pub tier: i64,
	#[serde(default)]
	pub element: Option<Element>,
	pub stats: BTreeMap<Stat, i64>,
	pub value: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AffixDef {
	pub id: String,
	pub stat: Stat,
	pub min: i64,
	pub max: i64,
	pub per_tier: i64,
	pub slots: Vec<Slot>,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct AchievementDef {
	pub id: String,
	#[serde(rename = "type")]
	pub kind: String,
	pub value: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DifficultyDef {
	pub id: String,
	pub hp_pct: i64,
	pub damage_pct: i64,
	pub gold_pct: i64,
	pub xp_pct: i64,
	pub non_common_weight_pct: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RarityDef {
	pub id: String,
	pub stat_pct: i64,
	pub value_pct: i64,
	pub affix_min: i64,
	pub affix_max: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpellLevelDef {
	pub level: i64,
	pub uses: i64,
	pub effect_pct: i64,
	pub mana_pct: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Caps {
	pub crit_chance: i64,
	pub dodge: i64,
	pub parry: i64,
	pub leech: i64,
	pub protection: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MagicLevelDef {
	pub base: i64,
	pub growth_pct: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StartingPotion {
	pub potion_id: String,
	pub quantity: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Balance {
	pub rounds_per_tier: i64,
	pub cycle_stat_pct: i64,
	pub cycle_reward_pct: i64,
	pub position_pct: i64,
	pub difficulties: Vec<DifficultyDef>,
	pub crit_multiplier_pct: i64,
	pub defend_damage_pct: i64,
	pub boss_telegraph_every: i64,
	pub boss_charge_damage_pct: i64,
	pub caps: Caps,
	pub magic_level: MagicLevelDef,
	pub spell_levels: Vec<SpellLevelDef>,
	pub starting_gold: i64,
	pub starting_potions: Vec<StartingPotion>,
	pub bag_capacity: i64,
	pub drop_chance_pct: i64,
	pub boss_drops: i64,
	pub rarities: Vec<RarityDef>,
	pub rarity_weights: BTreeMap<String, BTreeMap<String, i64>>,
	pub merchant_stock_size: i64,
	pub merchant_markup_pct: i64,
	pub spell_status_damage_pct: i64,
}

impl Balance {
	pub fn find_difficulty(&self, difficulty_id: &str) -> Option<&DifficultyDef> {
		self.difficulties.iter().find(|difficulty| difficulty.id == difficulty_id)
	}

	pub fn difficulty(&self, difficulty_id: &str) -> &DifficultyDef {
		self.find_difficulty(difficulty_id).unwrap_or_else(|| unknown_id(difficulty_id))
	}

	pub fn find_rarity(&self, rarity_id: &str) -> Option<&RarityDef> {
		self.rarities.iter().find(|rarity| rarity.id == rarity_id)
	}

	pub fn rarity(&self, rarity_id: &str) -> &RarityDef {
		self.find_rarity(rarity_id).unwrap_or_else(|| unknown_id(rarity_id))
	}
}

/// A lookup of an id the data itself references: a missing id means inconsistent data, which the data tests rule
/// out, so it panics (Python raises `UnknownIdError`). Use the `find_*` methods when the id comes from outside.
fn unknown_id(id: &str) -> ! {
	panic!("unknown id: {id}")
}

/// Indexes by id, built once. Maps are only used for lookups: game decisions iterate the `Vec`s in file order.
#[derive(Debug, Clone, Default)]
struct DataIndex {
	vocations: HashMap<String, usize>,
	spells: HashMap<String, usize>,
	/// Monsters first, then bosses (indices into `monsters.len() + boss index`).
	creatures: HashMap<String, usize>,
	potions: HashMap<String, usize>,
	statuses: HashMap<String, usize>,
	items: HashMap<String, usize>,
	/// Monster indices of each tier, sorted by id.
	monsters_by_tier: BTreeMap<i64, Vec<usize>>,
	bosses_by_tier: BTreeMap<i64, usize>,
}

fn index_by_id<T>(items: &[T], id: impl Fn(&T) -> &str) -> HashMap<String, usize> {
	items.iter().enumerate().map(|(index, item)| (id(item).to_owned(), index)).collect()
}

/// Every definition of the game. Built with [`GameData::new`] so the private index is always in sync.
#[derive(Debug, Clone)]
pub struct GameData {
	pub balance: Balance,
	pub vocations: Vec<VocationDef>,
	pub spells: Vec<SpellDef>,
	pub monsters: Vec<MonsterDef>,
	pub bosses: Vec<MonsterDef>,
	pub potions: Vec<PotionDef>,
	pub statuses: Vec<StatusDef>,
	pub items: Vec<ItemDef>,
	pub affixes: Vec<AffixDef>,
	pub achievements: Vec<AchievementDef>,
	pub families: Vec<String>,
	index: DataIndex,
}

/// The parts of [`GameData`], in the order of the Python dataclass.
#[derive(Debug, Clone)]
pub struct GameContent {
	pub balance: Balance,
	pub vocations: Vec<VocationDef>,
	pub spells: Vec<SpellDef>,
	pub monsters: Vec<MonsterDef>,
	pub bosses: Vec<MonsterDef>,
	pub potions: Vec<PotionDef>,
	pub statuses: Vec<StatusDef>,
	pub items: Vec<ItemDef>,
	pub affixes: Vec<AffixDef>,
	pub achievements: Vec<AchievementDef>,
	pub families: Vec<String>,
}

impl GameData {
	pub fn new(content: GameContent) -> GameData {
		let mut data = GameData {
			balance: content.balance,
			vocations: content.vocations,
			spells: content.spells,
			monsters: content.monsters,
			bosses: content.bosses,
			potions: content.potions,
			statuses: content.statuses,
			items: content.items,
			affixes: content.affixes,
			achievements: content.achievements,
			families: content.families,
			index: DataIndex::default(),
		};
		data.reindex();
		data
	}

	/// The content as plain parts, to build a modified copy (`dataclasses.replace` in Python tests).
	pub fn content(&self) -> GameContent {
		GameContent {
			balance: self.balance.clone(),
			vocations: self.vocations.clone(),
			spells: self.spells.clone(),
			monsters: self.monsters.clone(),
			bosses: self.bosses.clone(),
			potions: self.potions.clone(),
			statuses: self.statuses.clone(),
			items: self.items.clone(),
			affixes: self.affixes.clone(),
			achievements: self.achievements.clone(),
			families: self.families.clone(),
		}
	}

	fn reindex(&mut self) {
		let mut creatures = index_by_id(&self.monsters, |m| &m.id);
		for (index, boss) in self.bosses.iter().enumerate() {
			creatures.insert(boss.id.clone(), self.monsters.len() + index);
		}
		let mut monsters_by_tier: BTreeMap<i64, Vec<usize>> = BTreeMap::new();
		for (index, monster) in self.monsters.iter().enumerate() {
			monsters_by_tier.entry(monster.tier).or_default().push(index);
		}
		for group in monsters_by_tier.values_mut() {
			group.sort_by(|a, b| self.monsters[*a].id.cmp(&self.monsters[*b].id));
		}
		self.index = DataIndex {
			vocations: index_by_id(&self.vocations, |v| &v.id),
			spells: index_by_id(&self.spells, |s| &s.id),
			creatures,
			potions: index_by_id(&self.potions, |p| &p.id),
			statuses: index_by_id(&self.statuses, |s| &s.id),
			items: index_by_id(&self.items, |i| &i.id),
			monsters_by_tier,
			bosses_by_tier: self.bosses.iter().enumerate().map(|(index, boss)| (boss.tier, index)).collect(),
		};
	}

	pub fn tier_count(&self) -> i64 {
		self.bosses.len() as i64
	}

	pub fn find_vocation(&self, vocation_id: &str) -> Option<&VocationDef> {
		self.index.vocations.get(vocation_id).map(|&index| &self.vocations[index])
	}

	pub fn vocation(&self, vocation_id: &str) -> &VocationDef {
		self.find_vocation(vocation_id).unwrap_or_else(|| unknown_id(vocation_id))
	}

	pub fn find_spell(&self, spell_id: &str) -> Option<&SpellDef> {
		self.index.spells.get(spell_id).map(|&index| &self.spells[index])
	}

	pub fn spell(&self, spell_id: &str) -> &SpellDef {
		self.find_spell(spell_id).unwrap_or_else(|| unknown_id(spell_id))
	}

	pub fn find_creature(&self, creature_id: &str) -> Option<&MonsterDef> {
		let index = *self.index.creatures.get(creature_id)?;
		Some(if index < self.monsters.len() {
			&self.monsters[index]
		} else {
			&self.bosses[index - self.monsters.len()]
		})
	}

	pub fn creature(&self, creature_id: &str) -> &MonsterDef {
		self.find_creature(creature_id).unwrap_or_else(|| unknown_id(creature_id))
	}

	pub fn find_potion(&self, potion_id: &str) -> Option<&PotionDef> {
		self.index.potions.get(potion_id).map(|&index| &self.potions[index])
	}

	pub fn potion(&self, potion_id: &str) -> &PotionDef {
		self.find_potion(potion_id).unwrap_or_else(|| unknown_id(potion_id))
	}

	pub fn find_status(&self, status_id: &str) -> Option<&StatusDef> {
		self.index.statuses.get(status_id).map(|&index| &self.statuses[index])
	}

	pub fn status(&self, status_id: &str) -> &StatusDef {
		self.find_status(status_id).unwrap_or_else(|| unknown_id(status_id))
	}

	pub fn find_item(&self, item_id: &str) -> Option<&ItemDef> {
		self.index.items.get(item_id).map(|&index| &self.items[index])
	}

	pub fn item(&self, item_id: &str) -> &ItemDef {
		self.find_item(item_id).unwrap_or_else(|| unknown_id(item_id))
	}

	/// Monsters of a tier sorted by id (the order `spawn_monster` picks from).
	pub fn monsters_in_tier(&self, tier: i64) -> Vec<&MonsterDef> {
		self.index
			.monsters_by_tier
			.get(&tier)
			.map(|group| group.iter().map(|&index| &self.monsters[index]).collect())
			.unwrap_or_default()
	}

	pub fn boss_of_tier(&self, tier: i64) -> &MonsterDef {
		&self.bosses[self.index.bosses_by_tier[&tier]]
	}
}
