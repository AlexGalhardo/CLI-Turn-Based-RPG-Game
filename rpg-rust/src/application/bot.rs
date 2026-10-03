//! A deterministic heuristic player used by the simulator and by the end-to-end parity tests.
//!
//! The bot only reads the run state and returns one command at a time, so it can drive any engine implementation.
//! Its decisions are part of the golden "bot full run" files: changing them requires regenerating those files.

use crate::application::commands::Command;
use crate::application::loot::can_use;
use crate::application::merchant::available_potions;
use crate::application::run_state::RunState;
use crate::domain::character::{build_sheet, item_score, required_level};
use crate::domain::definitions::{GameData, PotionDef, SpellDef};
use crate::domain::entities::{ItemInstance, MonsterInstance};
use crate::domain::enums::{Phase, Resource, SpellKind};
use crate::domain::formulas::{pct, spell_level_for_uses};

pub const HEAL_THRESHOLD_PCT: i64 = 45;
pub const MANA_POTION_THRESHOLD_PCT: i64 = 25;
pub const MAX_POTION_STOCK: i64 = 20;

/// Python's `max(items, key=...)` keeps the first maximum; every key here ends with a unique id, so ties cannot
/// happen and `Iterator::max_by` (which keeps the last maximum) picks the same element.
pub struct GreedyBot<'a> {
	data: &'a GameData,
}

impl<'a> GreedyBot<'a> {
	pub fn new(data: &'a GameData) -> GreedyBot<'a> {
		GreedyBot { data }
	}

	pub fn choose(&self, state: &RunState) -> Command {
		if state.phase == Phase::Battle {
			return self.battle(state);
		}
		if state.phase == Phase::Victory {
			return Command::EndRun;
		}
		self.merchant(state)
	}

	// ── battle ────────────────────────────────────────────────────────────────

	fn battle(&self, state: &RunState) -> Command {
		let player = &state.player;
		let monster = state.monster.as_ref().expect("battle without a monster");
		let sheet = build_sheet(player, self.data);

		if self.charge_incoming(monster) {
			return Command::Defend;
		}
		if player.hp * 100 < sheet.max_hp * HEAL_THRESHOLD_PCT
			&& let Some(heal) = self.heal(state)
		{
			return heal;
		}
		if player.mp * 100 < sheet.max_mp * MANA_POTION_THRESHOLD_PCT
			&& let Some(potion) = self.best_owned_potion(state, Resource::Mp)
		{
			return Command::use_potion(&potion.id);
		}
		match self.best_attack_spell(state, monster) {
			Some(spell) => Command::cast(&spell.id),
			None => Command::Attack,
		}
	}

	fn charge_incoming(&self, monster: &MonsterInstance) -> bool {
		if !monster.is_boss {
			return false;
		}
		let every = self.data.balance.boss_telegraph_every;
		monster.boss_actions % (every + 1) == every
	}

	fn cost(&self, state: &RunState, spell: &SpellDef) -> i64 {
		let uses = state.player.spell_use_count(&spell.id);
		pct(spell.mana, spell_level_for_uses(uses, &self.data.balance.spell_levels).mana_pct)
	}

	fn spells(&self, state: &RunState, kind: SpellKind) -> Vec<&'a SpellDef> {
		let vocation = self.data.vocation(&state.player.vocation_id);
		vocation
			.spells
			.iter()
			.map(|spell_id| self.data.spell(spell_id))
			.filter(|spell| spell.kind == kind && self.cost(state, spell) <= state.player.mp)
			.collect()
	}

	fn heal(&self, state: &RunState) -> Option<Command> {
		let spells = self.spells(state, SpellKind::Heal);
		if let Some(best) = spells.iter().max_by(|a, b| (a.max, &a.id).cmp(&(b.max, &b.id))) {
			return Some(Command::cast(&best.id));
		}
		self.best_owned_potion(state, Resource::Hp).map(|potion| Command::use_potion(&potion.id))
	}

	fn best_owned_potion(&self, state: &RunState, resource: Resource) -> Option<&'a PotionDef> {
		self.data
			.potions
			.iter()
			.filter(|potion| potion.resource == resource && state.player.potion_count(&potion.id) > 0)
			.max_by(|a, b| (a.max, &a.id).cmp(&(b.max, &b.id)))
	}

	fn best_attack_spell(&self, state: &RunState, monster: &MonsterInstance) -> Option<&'a SpellDef> {
		let creature = self.data.creature(&monster.creature_id);
		self.spells(state, SpellKind::Attack).into_iter().filter(|spell| creature.resistance(spell.element) > 0).max_by(
			|a, b| {
				let key = |spell: &SpellDef| (spell.min + spell.max) * creature.resistance(spell.element);
				(key(a), &a.id).cmp(&(key(b), &b.id))
			},
		)
	}

	// ── merchant ──────────────────────────────────────────────────────────────

	fn merchant(&self, state: &RunState) -> Command {
		let player = &state.player;
		let vocation = self.data.vocation(&player.vocation_id);
		let mut bag: Vec<&ItemInstance> = player.bag.iter().collect();
		bag.sort_by_key(|item| item.uid);
		for item in &bag {
			let definition = self.data.item(&item.item_id);
			if !can_use(definition, vocation) || required_level(item, self.data) > player.level {
				continue;
			}
			let better = match player.equipment.get(&definition.slot) {
				None => true,
				Some(current) => item_score(item, self.data) > item_score(current, self.data),
			};
			if better {
				return Command::Equip { uid: item.uid };
			}
		}
		if let Some(first) = bag.first() {
			return Command::SellItem { uid: first.uid };
		}
		self.potion_purchase(state, Resource::Hp)
			.or_else(|| self.potion_purchase(state, Resource::Mp))
			.unwrap_or(Command::NextFight)
	}

	fn potion_purchase(&self, state: &RunState, resource: Resource) -> Option<Command> {
		let unlocked = available_potions(state, self.data);
		let options: Vec<&PotionDef> = self
			.data
			.potions
			.iter()
			.filter(|potion| potion.resource == resource && unlocked.contains(&potion.id))
			.collect();
		let best = options.iter().max_by(|a, b| (a.max, &a.id).cmp(&(b.max, &b.id)))?;
		let owned: i64 = options.iter().map(|potion| state.player.potion_count(&potion.id)).sum();
		let target = MAX_POTION_STOCK.min(5 + state.round / 5);
		let budget = if resource == Resource::Mp { state.player.gold / 2 } else { state.player.gold };
		let quantity = (target - owned).min(budget / best.price);
		if quantity <= 0 {
			return None;
		}
		Some(Command::buy_potion(&best.id, quantity))
	}
}
