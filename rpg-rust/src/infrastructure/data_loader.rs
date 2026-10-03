//! Loads shared/data/*.json into immutable domain definitions.

use std::fmt;

use serde::Deserialize;
use serde::de::DeserializeOwned;

use crate::assets::SharedFs;
use crate::domain::definitions::{
	AchievementDef, AffixDef, Balance, GameContent, GameData, ItemDef, MonsterDef, PotionDef, SpellDef, StatusDef,
	VocationDef,
};

/// A data file is missing, is not valid JSON, or lacks a field / has an invalid value.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DataError(pub String);

impl fmt::Display for DataError {
	fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
		formatter.write_str(&self.0)
	}
}

impl std::error::Error for DataError {}

fn read<T: DeserializeOwned>(shared: &SharedFs, name: &str) -> Result<T, DataError> {
	let path = format!("data/{name}.json");
	let text = shared.read(&path).ok_or_else(|| DataError(format!("{path}: file not found")))?;
	serde_json::from_str(text).map_err(|error| DataError(format!("{path}: {error}")))
}

/// Optional files (affixes, achievements) behave like an empty list when absent.
fn optional<T: DeserializeOwned + Default>(shared: &SharedFs, name: &str) -> Result<T, DataError> {
	if shared.read(&format!("data/{name}.json")).is_none() {
		return Ok(T::default());
	}
	read(shared, name)
}

#[derive(Deserialize)]
struct Vocations {
	vocations: Vec<VocationDef>,
}

#[derive(Deserialize)]
struct Spells {
	spells: Vec<SpellDef>,
}

#[derive(Deserialize)]
struct Monsters {
	monsters: Vec<MonsterDef>,
}

#[derive(Deserialize)]
struct Bosses {
	bosses: Vec<MonsterDef>,
}

#[derive(Deserialize)]
struct Potions {
	potions: Vec<PotionDef>,
}

#[derive(Deserialize)]
struct Statuses {
	statuses: Vec<StatusDef>,
}

#[derive(Deserialize)]
struct Items {
	items: Vec<ItemDef>,
}

#[derive(Deserialize, Default)]
struct Affixes {
	affixes: Vec<AffixDef>,
}

#[derive(Deserialize, Default)]
struct Achievements {
	achievements: Vec<AchievementDef>,
}

#[derive(Deserialize)]
struct Families {
	families: Vec<String>,
}

pub fn load_game_data(shared: &SharedFs) -> Result<GameData, DataError> {
	let mut bosses = read::<Bosses>(shared, "bosses")?.bosses;
	for boss in &mut bosses {
		boss.is_boss = true;
	}
	Ok(GameData::new(GameContent {
		balance: read::<Balance>(shared, "balance")?,
		vocations: read::<Vocations>(shared, "vocations")?.vocations,
		spells: read::<Spells>(shared, "spells")?.spells,
		monsters: read::<Monsters>(shared, "monsters")?.monsters,
		bosses,
		potions: read::<Potions>(shared, "potions")?.potions,
		statuses: read::<Statuses>(shared, "statuses")?.statuses,
		items: read::<Items>(shared, "items")?.items,
		affixes: optional::<Affixes>(shared, "affixes")?.affixes,
		achievements: optional::<Achievements>(shared, "achievements")?.achievements,
		families: read::<Families>(shared, "families")?.families,
	}))
}
