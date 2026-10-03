//! Closed sets of values. Serde renames keep the same strings as the JSON data and saves.

use std::cmp::Ordering;
use std::fmt;

use serde::{Deserialize, Serialize};

/// Implements `as_str()` and `Display` from one table, so each enum lists its JSON strings once.
macro_rules! string_enum {
	($name:ident { $($variant:ident => $text:literal),+ $(,)? }) => {
		impl $name {
			pub fn as_str(self) -> &'static str {
				match self {
					$($name::$variant => $text),+
				}
			}
		}

		impl fmt::Display for $name {
			fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
				formatter.write_str(self.as_str())
			}
		}
	};
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Element {
	Physical,
	Fire,
	Ice,
	Energy,
	Earth,
	Holy,
	Death,
}

string_enum!(Element {
	Physical => "physical",
	Fire => "fire",
	Ice => "ice",
	Energy => "energy",
	Earth => "earth",
	Holy => "holy",
	Death => "death",
});

/// Every element in declaration order (Python iterates `Element` in this order).
pub const ELEMENTS: [Element; 7] =
	[Element::Physical, Element::Fire, Element::Ice, Element::Energy, Element::Earth, Element::Holy, Element::Death];

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Slot {
	Helmet,
	Armor,
	Legs,
	Boots,
	Amulet,
	Ring,
	Weapon,
	Shield,
}

string_enum!(Slot {
	Helmet => "helmet",
	Armor => "armor",
	Legs => "legs",
	Boots => "boots",
	Amulet => "amulet",
	Ring => "ring",
	Weapon => "weapon",
	Shield => "shield",
});

/// Every slot in declaration order (screens list equipment in this order).
pub const SLOTS: [Slot; 8] =
	[Slot::Helmet, Slot::Armor, Slot::Legs, Slot::Boots, Slot::Amulet, Slot::Ring, Slot::Weapon, Slot::Shield];

// Equipment is a map keyed by slot; ordering by the string makes saves list slots alphabetically like the
// reference (`sorted(equipment.items())` compares the `StrEnum` values).
impl Ord for Slot {
	fn cmp(&self, other: &Self) -> Ordering {
		self.as_str().cmp(other.as_str())
	}
}

impl PartialOrd for Slot {
	fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
		Some(self.cmp(other))
	}
}

/// The equipment screen and auto-equip order (docs/game-design.md §8.1, docs/tui.md).
pub const EQUIPMENT_SLOT_ORDER: [Slot; 8] =
	[Slot::Weapon, Slot::Shield, Slot::Helmet, Slot::Armor, Slot::Legs, Slot::Boots, Slot::Ring, Slot::Amulet];

impl Slot {
	pub fn parse(text: &str) -> Option<Slot> {
		SLOTS.into_iter().find(|slot| slot.as_str() == text)
	}
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Phase {
	Merchant,
	Battle,
	Victory,
	GameOver,
}

string_enum!(Phase {
	Merchant => "merchant",
	Battle => "battle",
	Victory => "victory",
	GameOver => "game_over",
});

/// The class of a spawned monster: its row in `balance.enemyClasses` (docs/game-design.md §3).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EnemyClass {
	Normal,
	Elite,
	Boss,
}

string_enum!(EnemyClass {
	Normal => "normal",
	Elite => "elite",
	Boss => "boss",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Resource {
	Hp,
	Mp,
}

string_enum!(Resource {
	Hp => "hp",
	Mp => "mp",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SpellKind {
	Attack,
	Heal,
}

string_enum!(SpellKind {
	Attack => "attack",
	Heal => "heal",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum StatusKind {
	Dot,
	Stun,
}

string_enum!(StatusKind {
	Dot => "dot",
	Stun => "stun",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Target {
	Player,
	Monster,
}

string_enum!(Target {
	Player => "player",
	Monster => "monster",
});

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Stat {
	Attack,
	Armor,
	MaxHp,
	MaxMp,
	HpRegen,
	MpRegen,
	CritChance,
	CritDamage,
	SpellPower,
	PhysicalDamage,
	Dodge,
	Parry,
	LifeLeech,
	ManaLeech,
	ProtPhysical,
	ProtFire,
	ProtIce,
	ProtEnergy,
	ProtEarth,
	ProtHoly,
	ProtDeath,
}

string_enum!(Stat {
	Attack => "attack",
	Armor => "armor",
	MaxHp => "maxHp",
	MaxMp => "maxMp",
	HpRegen => "hpRegen",
	MpRegen => "mpRegen",
	CritChance => "critChance",
	CritDamage => "critDamage",
	SpellPower => "spellPower",
	PhysicalDamage => "physicalDamage",
	Dodge => "dodge",
	Parry => "parry",
	LifeLeech => "lifeLeech",
	ManaLeech => "manaLeech",
	ProtPhysical => "protPhysical",
	ProtFire => "protFire",
	ProtIce => "protIce",
	ProtEnergy => "protEnergy",
	ProtEarth => "protEarth",
	ProtHoly => "protHoly",
	ProtDeath => "protDeath",
});

/// The `prot<Element>` stat that reduces damage of an element.
pub fn protection_by_element(element: Element) -> Stat {
	match element {
		Element::Physical => Stat::ProtPhysical,
		Element::Fire => Stat::ProtFire,
		Element::Ice => Stat::ProtIce,
		Element::Energy => Stat::ProtEnergy,
		Element::Earth => Stat::ProtEarth,
		Element::Holy => Stat::ProtHoly,
		Element::Death => Stat::ProtDeath,
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn strings_match_the_json_values() {
		assert_eq!(serde_json::to_value(Stat::MaxHp).unwrap(), "maxHp");
		assert_eq!(serde_json::to_value(Phase::GameOver).unwrap(), "game_over");
		assert_eq!(Stat::ProtDeath.to_string(), "protDeath");
		assert_eq!(Element::Holy.to_string(), "holy");
		assert_eq!(Resource::Mp.to_string(), "mp");
		assert_eq!(SpellKind::Heal.to_string(), "heal");
		assert_eq!(StatusKind::Stun.to_string(), "stun");
		assert_eq!(Target::Monster.to_string(), "monster");
		assert_eq!(Phase::Battle.to_string(), "battle");
		assert_eq!(Phase::Victory.to_string(), "victory");
		assert_eq!(EnemyClass::Elite.to_string(), "elite");
		assert_eq!(serde_json::to_value(EnemyClass::Boss).unwrap(), "boss");
		for element in ELEMENTS {
			let stat = protection_by_element(element);
			assert_eq!(stat.as_str().to_lowercase(), format!("prot{element}"));
		}
	}

	#[test]
	fn slots_sort_alphabetically_and_parse() {
		let mut slots = SLOTS.to_vec();
		slots.sort();
		assert_eq!(slots.first(), Some(&Slot::Amulet));
		assert_eq!(slots.last(), Some(&Slot::Weapon));
		assert_eq!(Slot::parse("ring"), Some(Slot::Ring));
		assert_eq!(Slot::parse("tail"), None);
		assert_eq!(Slot::Boots.to_string(), "boots");
	}
}
