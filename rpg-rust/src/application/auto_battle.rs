//! Auto-battle policy (docs/game-design.md §13): picks the player's battle commands from the run state only.
//!
//! It lives in the application layer, not in the engine: the commands it returns are ordinary commands, so a fight
//! played by the policy replays like any other. Every command it returns is valid (affordable spells, owned potions).

use crate::application::commands::Command;
use crate::application::run_state::RunState;
use crate::domain::character::build_sheet;
use crate::domain::definitions::{AutoBattleDef, AutoBattleModeDef, GameData, PotionDef, SpellDef};
use crate::domain::enums::{Resource, SpellKind};
use crate::domain::formulas::{pct, spell_level_for_uses};

pub const OFFENSE_ATTACK: &str = "attack";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AutoBattleMode {
	Melee,
	Spells,
	Balanced,
}

/// Every mode in menu order (Python iterates the `StrEnum`).
pub const AUTO_BATTLE_MODES: [AutoBattleMode; 3] =
	[AutoBattleMode::Melee, AutoBattleMode::Spells, AutoBattleMode::Balanced];

impl AutoBattleMode {
	pub fn as_str(self) -> &'static str {
		match self {
			AutoBattleMode::Melee => "melee",
			AutoBattleMode::Spells => "spells",
			AutoBattleMode::Balanced => "balanced",
		}
	}
}

/// Something with a `max` and an `id`: spells and potions.
trait Strength {
	fn strength(&self) -> (i64, &str);
}

impl Strength for SpellDef {
	fn strength(&self) -> (i64, &str) {
		(self.max, &self.id)
	}
}

impl Strength for PotionDef {
	fn strength(&self) -> (i64, &str) {
		(self.max, &self.id)
	}
}

/// Highest `max`; ties go to the lowest id.
fn strongest<'a, T: Strength>(options: impl IntoIterator<Item = &'a T>) -> Option<&'a T> {
	options.into_iter().min_by(|a, b| {
		let ((a_max, a_id), (b_max, b_id)) = (a.strength(), b.strength());
		b_max.cmp(&a_max).then_with(|| a_id.cmp(b_id))
	})
}

pub struct AutoBattlePolicy<'a> {
	data: &'a GameData,
	mode: &'a AutoBattleModeDef,
	config: &'a AutoBattleDef,
}

impl<'a> AutoBattlePolicy<'a> {
	pub fn new(data: &'a GameData, mode: AutoBattleMode) -> AutoBattlePolicy<'a> {
		let config = &data.balance.auto_battle;
		AutoBattlePolicy { data, mode: config.mode(mode.as_str()), config }
	}

	pub fn choose(&self, state: &RunState) -> Command {
		let player = &state.player;
		let sheet = build_sheet(player, self.data);
		let config = self.config;

		if player.hp * 100 < sheet.max_hp * config.emergency_heal_below_pct
			&& let Some(heal) = self.heal(state)
		{
			return heal;
		}
		let every = self.mode.support_every;
		if state.turn % every == every - 1 {
			if player.hp * 100 < sheet.max_hp * config.heal_below_pct
				&& let Some(heal) = self.heal(state)
			{
				return heal;
			}
			if player.mp * 100 < sheet.max_mp * config.mana_below_pct
				&& let Some(potion) = self.best_potion(state, Resource::Mp)
			{
				return Command::use_potion(&potion.id);
			}
			if self.telegraph_pending(state) {
				return Command::Defend;
			}
		}
		self.offense(state)
	}

	fn offense(&self, state: &RunState) -> Command {
		if self.mode.offense == OFFENSE_ATTACK {
			return Command::Attack;
		}
		match strongest(self.affordable(state, SpellKind::Attack)) {
			Some(spell) => Command::cast(&spell.id),
			None => Command::Attack,
		}
	}

	fn heal(&self, state: &RunState) -> Option<Command> {
		if let Some(spell) = strongest(self.affordable(state, SpellKind::Heal)) {
			return Some(Command::cast(&spell.id));
		}
		self.best_potion(state, Resource::Hp).map(|potion| Command::use_potion(&potion.id))
	}

	fn affordable(&self, state: &RunState, kind: SpellKind) -> Vec<&'a SpellDef> {
		let player = &state.player;
		let levels = &self.data.balance.spell_levels;
		self.data
			.vocation(&player.vocation_id)
			.spells
			.iter()
			.map(|spell_id| self.data.spell(spell_id))
			.filter(|spell| {
				spell.kind == kind
					&& pct(spell.mana, spell_level_for_uses(player.spell_use_count(&spell.id), levels).mana_pct)
						<= player.mp
			})
			.collect()
	}

	fn best_potion(&self, state: &RunState, resource: Resource) -> Option<&'a PotionDef> {
		strongest(
			self.data
				.potions
				.iter()
				.filter(|potion| potion.resource == resource && state.player.potion_count(&potion.id) > 0),
		)
	}

	/// The boss announced its charged attack: its next action is the charge.
	fn telegraph_pending(&self, state: &RunState) -> bool {
		let Some(monster) = &state.monster else {
			return false;
		};
		if !monster.is_boss {
			return false;
		}
		let every = self.data.balance.boss_telegraph_every;
		monster.boss_actions % (every + 1) == every
	}
}
