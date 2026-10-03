//! Cross-run profile: bestiary, achievements and Hall of Fame (docs/game-design.md §11).

use std::collections::BTreeMap;
use std::rc::Rc;

use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::application::events::Event;
use crate::application::run_state::RunState;
use crate::application::save_game::{PersistenceError, check_schema};
use crate::domain::definitions::{AchievementDef, GameData};

pub const PROFILE_SCHEMA_VERSION: i64 = 1;
pub const HALL_OF_FAME_SIZE: usize = 10;
pub const BESTIARY_REVEAL_KILLS: i64 = 5;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BestiaryEntry {
	pub kills: i64,
	pub first_killed_at: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Unlock {
	pub unlocked_at: String,
	pub run_id: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HallOfFameEntry {
	pub run_id: String,
	pub name: String,
	pub vocation: String,
	pub difficulty: String,
	pub round: i64,
	pub level: i64,
	pub ended_at: String,
}

fn profile_schema_version() -> i64 {
	PROFILE_SCHEMA_VERSION
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Profile {
	#[serde(default = "profile_schema_version")]
	pub schema_version: i64,
	pub bestiary: BTreeMap<String, BestiaryEntry>,
	pub achievements: BTreeMap<String, Unlock>,
	pub hall_of_fame: Vec<HallOfFameEntry>,
}

impl Default for Profile {
	fn default() -> Profile {
		Profile {
			schema_version: PROFILE_SCHEMA_VERSION,
			bestiary: BTreeMap::new(),
			achievements: BTreeMap::new(),
			hall_of_fame: Vec::new(),
		}
	}
}

impl Profile {
	pub fn from_json(raw: &Value) -> Result<Profile, PersistenceError> {
		check_schema(raw, "profile.json")?;
		Profile::deserialize(raw).map_err(|error| PersistenceError::Invalid(format!("profile.json: {error}")))
	}
}

/// Feeds the profile from engine events. Returns the achievements unlocked by each step.
#[derive(Debug, Clone)]
pub struct ProfileService {
	data: Rc<GameData>,
	pub profile: Profile,
}

impl ProfileService {
	pub fn new(data: Rc<GameData>, profile: Profile) -> ProfileService {
		ProfileService { data, profile }
	}

	pub fn observe(&mut self, events: &[Event], state: &RunState, now: &str, run_id: &str) -> Vec<AchievementDef> {
		for event in events {
			if let Event::MonsterKilled { monster_id, .. } = event {
				self.profile
					.bestiary
					.entry(monster_id.clone())
					.and_modify(|entry| entry.kills += 1)
					.or_insert_with(|| BestiaryEntry { kills: 1, first_killed_at: now.to_owned() });
			}
		}
		let mut unlocked = Vec::new();
		for achievement in &self.data.achievements {
			if self.profile.achievements.contains_key(&achievement.id) {
				continue;
			}
			if self.progress(achievement, state) >= achievement.value {
				self.profile
					.achievements
					.insert(achievement.id.clone(), Unlock { unlocked_at: now.to_owned(), run_id: run_id.to_owned() });
				unlocked.push(achievement.clone());
			}
		}
		unlocked
	}

	fn progress(&self, achievement: &AchievementDef, state: &RunState) -> i64 {
		let player = &state.player;
		let bestiary = &self.profile.bestiary;
		match achievement.kind.as_str() {
			"kills_total" => bestiary.values().map(|entry| entry.kills).sum(),
			"bosses_total" => {
				let is_boss = |id: &str| self.data.bosses.iter().any(|boss| boss.id == id);
				bestiary.iter().filter(|(id, _)| is_boss(id)).map(|(_, entry)| entry.kills).sum()
			}
			"round_reached" => state.round,
			"level_reached" => player.level,
			"legendary_found" => state.stats.items_dropped.get("legendary").copied().unwrap_or(0),
			"spell_level_3" => {
				let threshold = self.data.balance.spell_levels.last().map_or(0, |level| level.uses);
				player.spell_uses.values().filter(|&&uses| uses >= threshold).count() as i64
			}
			"gold_held" => player.gold,
			"distinct_monsters" => bestiary.len() as i64,
			"hard_round_reached" if state.config.difficulty_id == "hard" => state.round,
			_ => 0,
		}
	}

	/// Keeps the top entries by round, then level, then earliest end (a stable sort, like Python's `sorted`).
	pub fn record_finished_run(&mut self, entry: HallOfFameEntry) {
		let ranking = &mut self.profile.hall_of_fame;
		ranking.push(entry);
		ranking.sort_by(|a, b| {
			b.round.cmp(&a.round).then(b.level.cmp(&a.level)).then_with(|| a.ended_at.cmp(&b.ended_at))
		});
		ranking.truncate(HALL_OF_FAME_SIZE);
	}

	pub fn revealed(&self, monster_id: &str) -> bool {
		self.profile.bestiary.get(monster_id).is_some_and(|entry| entry.kills >= BESTIARY_REVEAL_KILLS)
	}
}
