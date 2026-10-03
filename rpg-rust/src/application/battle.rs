//! Battle resolution, following docs/game-design.md §6. Every RNG call here is part of the contract.

use crate::application::commands::Command;
use crate::application::events::{ErrorCode, Event};
use crate::application::progression::Progression;
use crate::application::run_state::RunState;
use crate::domain::character::{CharacterSheet, build_sheet};
use crate::domain::definitions::{EnemyClassDef, GameData, MonsterAttack, SpellDef};
use crate::domain::entities::{ActiveStatus, MonsterInstance, Player};
use crate::domain::enums::{Element, Resource, SpellKind, StatusKind, Target};
use crate::domain::formulas::{armor_mitigation, pct, spell_level_for_uses};
use crate::domain::rng::Rng;

pub const STUN: &str = "stun";
pub const STUN_COOLDOWN_TURNS: i64 = 2;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BattleOutcome {
	Ongoing,
	Victory,
	Defeat,
}

/// One battle turn over borrowed engine parts. The borrow checker guarantees nothing else touches the state or
/// the PRNG while a turn resolves.
pub struct Battle<'a> {
	data: &'a GameData,
	rng: &'a mut Rng,
	state: &'a mut RunState,
}

impl<'a> Battle<'a> {
	pub fn new(data: &'a GameData, rng: &'a mut Rng, state: &'a mut RunState) -> Battle<'a> {
		Battle { data, rng, state }
	}

	fn player(&mut self) -> &mut Player {
		&mut self.state.player
	}

	fn monster(&mut self) -> &mut MonsterInstance {
		self.state.monster.as_mut().expect("battle without a monster")
	}

	fn monster_ref(&self) -> &MonsterInstance {
		self.state.monster.as_ref().expect("battle without a monster")
	}

	fn sheet(&self) -> CharacterSheet {
		build_sheet(&self.state.player, self.data)
	}

	fn class(&self) -> &'a EnemyClassDef {
		self.data.balance.enemy_class(self.monster_ref().enemy_class)
	}

	// ── validation ────────────────────────────────────────────────────────────

	/// Returns an error event for an invalid command. Validation never consumes randomness.
	pub fn validate(&self, command: &Command) -> Option<Event> {
		let player = &self.state.player;
		match command {
			Command::Cast { spell_id } => {
				let vocation = self.data.vocation(&player.vocation_id);
				if !vocation.spells.contains(spell_id) {
					return Some(Event::error(ErrorCode::UnknownSpell));
				}
				if player.mp < self.spell_cost(self.data.spell(spell_id)) {
					return Some(Event::error(ErrorCode::NotEnoughMana));
				}
			}
			Command::UsePotion { potion_id } => {
				if self.data.find_potion(potion_id).is_none() {
					return Some(Event::error(ErrorCode::UnknownPotion));
				}
				if player.potion_count(potion_id) <= 0 {
					return Some(Event::error(ErrorCode::NoPotion));
				}
			}
			_ => {}
		}
		None
	}

	fn spell_cost(&self, spell: &SpellDef) -> i64 {
		let uses = self.state.player.spell_use_count(&spell.id);
		pct(spell.mana, spell_level_for_uses(uses, &self.data.balance.spell_levels).mana_pct)
	}

	// ── turn ──────────────────────────────────────────────────────────────────

	pub fn play_turn(&mut self, command: &Command) -> (Vec<Event>, BattleOutcome) {
		let mut events = Vec::new();
		self.player_action(command, &mut events);
		loop {
			if let Some(outcome) = self.death_check() {
				return (events, outcome);
			}
			self.monster_phase(&mut events);
			if let Some(outcome) = self.death_check() {
				return (events, outcome);
			}
			if self.end_of_turn(&mut events) {
				return (events, BattleOutcome::Defeat);
			}
			if !consume_stun(&mut self.state.player.statuses) {
				return (events, BattleOutcome::Ongoing);
			}
			self.player().stun_cooldown = STUN_COOLDOWN_TURNS;
			events.push(Event::PlayerStunned);
		}
	}

	/// Steps 2 and 4: a parried hit can kill the attacker, so the player is checked first.
	fn death_check(&self) -> Option<BattleOutcome> {
		if self.state.player.hp <= 0 {
			return Some(BattleOutcome::Defeat);
		}
		if self.monster_ref().hp <= 0 {
			return Some(BattleOutcome::Victory);
		}
		None
	}

	// ── step 1: player action ─────────────────────────────────────────────────

	fn player_action(&mut self, command: &Command, events: &mut Vec<Event>) {
		match command {
			Command::Attack => self.melee(events),
			Command::Cast { spell_id } => self.cast(self.data.spell(spell_id), events),
			Command::UsePotion { potion_id } => self.drink(potion_id, events),
			Command::Defend => {
				self.player().defending = true;
				events.push(Event::PlayerDefended);
			}
			_ => unreachable!("not a battle command: {command:?}"),
		}
	}

	fn melee(&mut self, events: &mut Vec<Event>) {
		let sheet = self.sheet();
		if self.monster_dodges(events) {
			return;
		}
		let damage = pct(self.rng.roll(sheet.melee_min, sheet.melee_max), 100 + sheet.physical_damage);
		let (damage, crit) = self.roll_crit(damage, &sheet);
		let damage = self.resisted(damage, sheet.weapon_element);
		if self.monster_parries(damage, sheet.weapon_element, events) {
			return;
		}
		self.hit_monster(damage);
		events.push(Event::PlayerAttacked { damage, crit, element: sheet.weapon_element });
		self.leech(damage, &sheet, events);
	}

	fn monster_dodges(&mut self, events: &mut Vec<Event>) -> bool {
		if !self.rng.chance(self.class().dodge) {
			return false;
		}
		events.push(Event::MonsterDodged);
		true
	}

	/// Physical hits only: the monster takes nothing and reflects part of the hit (no mitigation).
	fn monster_parries(&mut self, damage: i64, element: Element, events: &mut Vec<Event>) -> bool {
		if element != Element::Physical || !self.rng.chance(self.class().parry) {
			return false;
		}
		let reflected = 1.max(pct(damage, self.data.balance.parry_reflect_pct));
		let player = self.player();
		player.hp = 0.max(player.hp - reflected);
		events.push(Event::MonsterParried { reflected });
		true
	}

	fn after_cast(&mut self, spell: &SpellDef, cost: i64, events: &mut Vec<Event>) {
		events.extend(Progression::new(self.data).after_cast(&mut self.state.player, spell, cost));
	}

	fn cast(&mut self, spell: &SpellDef, events: &mut Vec<Event>) {
		let sheet = self.sheet();
		let level = spell_level_for_uses(self.state.player.spell_use_count(&spell.id), &self.data.balance.spell_levels);
		let cost = pct(spell.mana, level.mana_pct);
		self.player().mp -= cost;
		if spell.kind == SpellKind::Attack && self.monster_dodges(events) {
			self.after_cast(spell, cost, events);
			return;
		}
		let player = &self.state.player;
		let bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level;
		let amount =
			pct(pct(self.rng.roll(spell.min + bonus, spell.max + bonus), level.effect_pct), 100 + sheet.spell_power);

		if spell.kind == SpellKind::Attack {
			let (damage, crit) = self.roll_crit(amount, &sheet);
			let damage = self.resisted(damage, spell.element);
			if self.monster_parries(damage, spell.element, events) {
				self.after_cast(spell, cost, events);
				return;
			}
			self.hit_monster(damage);
			events.push(Event::SpellCast {
				spell_id: spell.id.clone(),
				damage,
				crit,
				element: spell.element,
				mana: cost,
			});
			self.leech(damage, &sheet, events);
			let bonus_effect = &spell.level3_bonus;
			if let Some(status) = &bonus_effect.status
				&& level.level == 3
				&& self.rng.chance(bonus_effect.chance)
			{
				let per_turn = 1.max(pct(damage, self.data.balance.spell_status_damage_pct));
				self.apply_status(Target::Monster, status, per_turn, events);
			}
		} else {
			let player = self.player();
			let healed = amount.min(sheet.max_hp - player.hp);
			player.hp += healed;
			events.push(Event::SpellHealed { spell_id: spell.id.clone(), amount: healed, mana: cost });
			if level.level == 3 && spell.level3_bonus.cleanse {
				for status in std::mem::take(&mut player.statuses) {
					events.push(Event::StatusExpired { target: Target::Player, status: status.status_id });
				}
			}
		}

		self.after_cast(spell, cost, events);
	}

	fn drink(&mut self, potion_id: &str, events: &mut Vec<Event>) {
		let sheet = self.sheet();
		let potion = self.data.potion(potion_id);
		*self.player().potions.get_mut(potion_id).expect("validated potion") -= 1;
		let amount = self.rng.roll(potion.min, potion.max);
		let player = self.player();
		let restored = if potion.resource == Resource::Hp {
			let restored = amount.min(sheet.max_hp - player.hp);
			player.hp += restored;
			restored
		} else {
			let restored = amount.min(sheet.max_mp - player.mp);
			player.mp += restored;
			restored
		};
		events.push(Event::PotionUsed { potion_id: potion_id.to_owned(), amount: restored, resource: potion.resource });
	}

	fn roll_crit(&mut self, damage: i64, sheet: &CharacterSheet) -> (i64, bool) {
		if self.rng.chance(sheet.crit_chance) {
			return (pct(damage, self.data.balance.crit_multiplier_pct + sheet.crit_damage), true);
		}
		(damage, false)
	}

	fn monster_resistance(&self, element: Element) -> i64 {
		self.data.creature(&self.monster_ref().creature_id).resistance(element)
	}

	fn resisted(&self, damage: i64, element: Element) -> i64 {
		let resistance = self.monster_resistance(element);
		if resistance == 0 {
			return 0;
		}
		1.max(pct(damage, resistance))
	}

	fn hit_monster(&mut self, damage: i64) {
		let monster = self.monster();
		monster.hp = 0.max(monster.hp - damage);
	}

	fn leech(&mut self, damage: i64, sheet: &CharacterSheet, events: &mut Vec<Event>) {
		let player = self.player();
		let hp_gain = pct(damage, sheet.life_leech).min(sheet.max_hp - player.hp);
		let mp_gain = pct(damage, sheet.mana_leech).min(sheet.max_mp - player.mp);
		if hp_gain <= 0 && mp_gain <= 0 {
			return;
		}
		player.hp += 0.max(hp_gain);
		player.mp += 0.max(mp_gain);
		events.push(Event::Leeched { hp: 0.max(hp_gain), mp: 0.max(mp_gain) });
	}

	// ── step 3: monster phase ─────────────────────────────────────────────────

	fn monster_phase(&mut self, events: &mut Vec<Event>) {
		self.tick(Target::Monster, events);
		if self.monster_ref().hp <= 0 {
			return;
		}
		if consume_stun(&mut self.monster().statuses) {
			self.monster().stun_cooldown = STUN_COOLDOWN_TURNS;
			events.push(Event::MonsterStunned);
			return;
		}
		// A healing monster does nothing else this turn; a boss does not advance its pattern.
		let heal_pct = self.data.balance.monster_heal_pct;
		let heal_chance = self.class().heal;
		let monster = self.monster_ref();
		if monster.hp < monster.max_hp && self.rng.chance(heal_chance) {
			let monster = self.monster();
			let healed = pct(monster.max_hp, heal_pct).min(monster.max_hp - monster.hp);
			monster.hp += healed;
			events.push(Event::MonsterHealed { amount: healed });
			return;
		}

		if self.monster_ref().is_boss {
			let every = self.data.balance.boss_telegraph_every;
			let monster = self.monster();
			let position = monster.boss_actions % (every + 1);
			monster.boss_actions += 1;
			let charge_id = self.data.creature(&self.monster_ref().creature_id).charge_attack.as_deref();
			if let Some(charge_id) = charge_id {
				if position == every - 1 {
					let charge = self.monster_ref().attack(charge_id);
					events.push(Event::BossTelegraph { attack_id: charge.id.clone(), element: charge.element });
					return;
				}
				if position == every {
					let charge = self.monster_ref().attack(charge_id).clone();
					self.resolve_monster_attack(&charge, true, events);
					return;
				}
			}
		}

		let weights: Vec<i64> = self.monster_ref().attacks.iter().map(|attack| attack.weight).collect();
		let index = self.rng.weighted(&weights);
		let attack = self.monster_ref().attacks[index].clone();
		self.resolve_monster_attack(&attack, false, events);
	}

	fn resolve_monster_attack(&mut self, attack: &MonsterAttack, charged: bool, events: &mut Vec<Event>) {
		let sheet = self.sheet();
		if self.rng.chance(sheet.dodge) {
			events.push(Event::AttackDodged { attack_id: attack.id.clone() });
			return;
		}
		let data = self.data;
		let balance = &data.balance;
		let mut damage = self.rng.roll(attack.min, attack.max);
		if charged {
			damage = pct(damage, balance.boss_charge_damage_pct);
		}
		if attack.element == Element::Physical && self.rng.chance(sheet.parry) {
			let reflected = 1.max(pct(damage, balance.parry_reflect_pct));
			self.hit_monster(reflected);
			events.push(Event::AttackParried { attack_id: attack.id.clone(), reflected });
			return;
		}
		let crit = self.rng.chance(self.class().crit);
		if crit {
			damage = pct(damage, balance.crit_multiplier_pct);
		}
		if attack.element == Element::Physical {
			damage = armor_mitigation(damage, sheet.armor);
		}
		damage = pct(damage, 100 - sheet.protection(attack.element));
		if self.state.player.defending {
			damage = pct(damage, self.data.balance.defend_damage_pct);
		}
		let damage = 1.max(damage);
		let player = self.player();
		player.hp = 0.max(player.hp - damage);
		events.push(Event::MonsterAttacked {
			attack_id: attack.id.clone(),
			damage,
			element: attack.element,
			charged,
			crit,
		});
		if let Some(status) = &attack.status
			&& self.rng.chance(status.chance)
		{
			let per_turn = 1.max(pct(damage, status.damage_pct));
			self.apply_status(Target::Player, &status.status, per_turn, events);
		}
	}

	// ── step 5: end of turn ───────────────────────────────────────────────────

	/// Returns true when the player died from status ticks.
	fn end_of_turn(&mut self, events: &mut Vec<Event>) -> bool {
		self.tick(Target::Player, events);
		if self.state.player.hp <= 0 {
			return true;
		}
		let sheet = self.sheet();
		let player = self.player();
		let hp_gain = 0.max(sheet.hp_regen.min(sheet.max_hp - player.hp));
		let mp_gain = 0.max(sheet.mp_regen.min(sheet.max_mp - player.mp));
		player.hp += hp_gain;
		player.mp += mp_gain;
		if hp_gain > 0 || mp_gain > 0 {
			events.push(Event::Regenerated { hp: hp_gain, mp: mp_gain });
		}
		player.defending = false;
		player.stun_cooldown = 0.max(player.stun_cooldown - 1);
		let monster = self.monster();
		monster.stun_cooldown = 0.max(monster.stun_cooldown - 1);
		self.state.turn += 1;
		false
	}

	// ── statuses ──────────────────────────────────────────────────────────────

	fn statuses(&mut self, target: Target) -> &mut Vec<ActiveStatus> {
		match target {
			Target::Player => &mut self.state.player.statuses,
			Target::Monster => &mut self.monster().statuses,
		}
	}

	fn apply_status(&mut self, target: Target, status_id: &str, per_turn: i64, events: &mut Vec<Event>) {
		let definition = self.data.status(status_id);
		let cooldown = match target {
			Target::Player => self.state.player.stun_cooldown,
			Target::Monster => {
				if self.monster_resistance(definition.element) == 0 {
					return;
				}
				self.monster_ref().stun_cooldown
			}
		};
		let statuses = self.statuses(target);

		if definition.kind == StatusKind::Stun {
			if cooldown > 0 || statuses.iter().any(|status| status.status_id == STUN) {
				return;
			}
			statuses.push(ActiveStatus::new(STUN, definition.turns, 0));
			events.push(Event::StatusApplied { target, status: STUN.to_owned(), turns: definition.turns, per_turn: 0 });
			return;
		}

		let existing = match statuses.iter_mut().position(|status| status.status_id == status_id) {
			Some(index) => {
				let existing = &mut statuses[index];
				existing.turns = definition.turns;
				existing.per_turn = existing.per_turn.max(per_turn);
				existing.clone()
			}
			None => {
				let created = ActiveStatus::new(status_id, definition.turns, per_turn);
				statuses.push(created.clone());
				created
			}
		};
		events.push(Event::StatusApplied {
			target,
			status: status_id.to_owned(),
			turns: existing.turns,
			per_turn: existing.per_turn,
		});
	}

	/// Damage-over-time ticks. Python iterates a copy of the list and removes expired entries; walking indices
	/// and only advancing when nothing was removed is the same thing without the copy.
	fn tick(&mut self, target: Target, events: &mut Vec<Event>) {
		let mut index = 0;
		while index < self.statuses(target).len() {
			let ActiveStatus { status_id, per_turn, .. } = self.statuses(target)[index].clone();
			let definition = self.data.status(&status_id);
			if definition.kind != StatusKind::Dot {
				index += 1;
				continue;
			}
			let damage = self.status_damage(target, per_turn, definition.element);
			match target {
				Target::Player => {
					let player = self.player();
					player.hp = 0.max(player.hp - damage);
				}
				Target::Monster => self.hit_monster(damage),
			}
			events.push(Event::StatusTicked { target, status: status_id.clone(), damage });
			let statuses = self.statuses(target);
			statuses[index].turns -= 1;
			if statuses[index].turns <= 0 {
				statuses.remove(index);
				events.push(Event::StatusExpired { target, status: status_id });
			} else {
				index += 1;
			}
		}
	}

	fn status_damage(&self, target: Target, per_turn: i64, element: Element) -> i64 {
		match target {
			Target::Player => 1.max(pct(per_turn, 100 - self.sheet().protection(element))),
			Target::Monster => {
				let resistance = self.monster_resistance(element);
				if resistance == 0 { 0 } else { 1.max(pct(per_turn, resistance)) }
			}
		}
	}
}

fn consume_stun(statuses: &mut Vec<ActiveStatus>) -> bool {
	match statuses.iter().position(|status| status.status_id == STUN) {
		Some(index) => {
			statuses.remove(index);
			true
		}
		None => false,
	}
}
