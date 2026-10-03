//! The game engine: a pure state machine `step(command) -> events` (docs/architecture.md).

use std::fmt;
use std::rc::Rc;

use crate::application::auto_equip::auto_equip;
use crate::application::battle::{Battle, BattleOutcome};
use crate::application::commands::Command;
use crate::application::events::{ErrorCode, Event};
use crate::application::loot::generate_item;
use crate::application::merchant::Merchant;
use crate::application::progression::Progression;
use crate::application::run_state::{RunConfig, RunState};
use crate::application::spawner::spawn_monster;
use crate::domain::character::{build_sheet, item_value};
use crate::domain::definitions::{EnemyClassDef, GameData};
use crate::domain::entities::{ItemInstance, MonsterInstance, Player};
use crate::domain::enums::Phase;
use crate::domain::formulas::round_info;
use crate::domain::rng::Rng;

/// A run configuration that references a vocation or difficulty the data does not have.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InvalidRunConfig(pub String);

impl fmt::Display for InvalidRunConfig {
	fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
		// Same text as Python's `f"invalid run config: {KeyError(id)}"`.
		write!(formatter, "invalid run config: '{}'", self.0)
	}
}

impl std::error::Error for InvalidRunConfig {}

/// The engine owns the run state and the PRNG; the game data is shared (`Rc`) with the session and the UI.
#[derive(Debug, Clone)]
pub struct GameEngine {
	data: Rc<GameData>,
	state: RunState,
	rng: Rng,
}

impl GameEngine {
	pub fn new(data: Rc<GameData>, state: RunState, rng: Rng) -> GameEngine {
		GameEngine { data, state, rng }
	}

	pub fn state(&self) -> &RunState {
		&self.state
	}

	/// Mutable access for tests and tools that set up scenarios (the UI never mutates the state).
	pub fn state_mut(&mut self) -> &mut RunState {
		&mut self.state
	}

	pub fn data(&self) -> &Rc<GameData> {
		&self.data
	}

	pub fn rng_state(&self) -> u32 {
		self.rng.state()
	}

	pub fn new_run(
		data: Rc<GameData>,
		config: RunConfig,
		seed: u64,
	) -> Result<(GameEngine, Vec<Event>), InvalidRunConfig> {
		let vocation =
			data.find_vocation(&config.vocation_id).ok_or_else(|| InvalidRunConfig(config.vocation_id.clone()))?;
		if data.balance.find_difficulty(&config.difficulty_id).is_none() {
			return Err(InvalidRunConfig(config.difficulty_id.clone()));
		}

		let mut player =
			Player::new(&config.name, &vocation.id, vocation.start_hp, vocation.start_mp, data.balance.starting_gold);
		player.potions =
			data.balance.starting_potions.iter().map(|potion| (potion.potion_id.clone(), potion.quantity)).collect();
		let starter = data.item(&vocation.starter_weapon);
		let mut state = RunState::new(seed, config, player);
		let uid = state.take_item_uid();
		state.player.equipment.insert(starter.slot, ItemInstance::new(uid, &starter.id, "common", starter.tier));
		let mut events = vec![Event::RunStarted {
			seed,
			vocation: vocation.id.clone(),
			difficulty: state.config.difficulty_id.clone(),
		}];
		let mut engine = GameEngine::new(data, state, Rng::new(seed));
		events.extend(Merchant::new(&engine.data, &mut engine.rng, &mut engine.state).enter());
		Ok((engine, events))
	}

	pub fn restore(data: Rc<GameData>, state: RunState, rng_state: u32) -> GameEngine {
		GameEngine::new(data, state, Rng::new(u64::from(rng_state)))
	}

	pub fn step(&mut self, command: &Command) -> Vec<Event> {
		let events = self.dispatch(command);
		self.state.stats.record(&events, self.state.round);
		events
	}

	fn dispatch(&mut self, command: &Command) -> Vec<Event> {
		let phase = self.state.phase;
		match command {
			Command::Attack | Command::Cast { .. } | Command::UsePotion { .. } | Command::Defend => {
				if phase != Phase::Battle {
					return vec![Event::error(ErrorCode::InvalidPhase)];
				}
				self.battle_turn(command)
			}
			Command::NextFight => {
				if phase != Phase::Merchant {
					return vec![Event::error(ErrorCode::InvalidPhase)];
				}
				self.next_fight()
			}
			Command::BuyPotion { .. }
			| Command::SellItem { .. }
			| Command::Equip { .. }
			| Command::Unequip { .. }
			| Command::BuyStockItem { .. } => {
				if phase != Phase::Merchant {
					return vec![Event::error(ErrorCode::InvalidPhase)];
				}
				Merchant::new(&self.data, &mut self.rng, &mut self.state).handle(command)
			}
			Command::EndRun | Command::ContinueRun => {
				if phase != Phase::Victory {
					return vec![Event::error(ErrorCode::InvalidPhase)];
				}
				if *command == Command::EndRun { self.end_run() } else { self.continue_run() }
			}
		}
	}

	fn next_fight(&mut self) -> Vec<Event> {
		let state = &mut self.state;
		state.round += 1;
		let difficulty = self.data.balance.difficulty(&state.config.difficulty_id);
		let (monster, info) = spawn_monster(&self.data, &mut self.rng, state.round, difficulty);
		let event = Event::RoundStarted {
			round: state.round,
			tier: info.tier,
			cycle: info.cycle,
			monster_id: monster.creature_id.clone(),
			is_boss: monster.is_boss,
			enemy_class: monster.enemy_class,
			hp: monster.hp,
		};
		state.monster = Some(monster);
		state.phase = Phase::Battle;
		state.turn = 1;
		state.merchant_stock = Vec::new();
		vec![event]
	}

	fn battle_turn(&mut self, command: &Command) -> Vec<Event> {
		let mut battle = Battle::new(&self.data, &mut self.rng, &mut self.state);
		if let Some(invalid) = battle.validate(command) {
			return vec![invalid];
		}
		let (mut events, outcome) = battle.play_turn(command);
		match outcome {
			BattleOutcome::Victory => events.extend(self.victory()),
			BattleOutcome::Defeat => events.extend(self.defeat()),
			BattleOutcome::Ongoing => {}
		}
		events
	}

	fn victory(&mut self) -> Vec<Event> {
		let monster = self.state.monster.clone().expect("victory without a monster");
		let mut events = vec![Event::MonsterKilled {
			monster_id: monster.creature_id.clone(),
			is_boss: monster.is_boss,
			enemy_class: monster.enemy_class,
		}];
		events.extend(Progression::new(&self.data).gain_experience(&mut self.state.player, monster.xp));

		let gold = self.rng.roll(monster.gold_min, monster.gold_max);
		self.state.player.gold += gold;
		events.push(Event::GoldLooted { amount: gold });
		events.extend(self.drops(&monster));
		if self.state.config.auto_equip {
			events.extend(auto_equip(&mut self.state, &self.data));
		}

		let player = &mut self.state.player;
		player.statuses.clear();
		player.stun_cooldown = 0;
		player.defending = false;
		let sheet = build_sheet(player, &self.data);
		player.hp = player.hp.min(sheet.max_hp);
		player.mp = player.mp.min(sheet.max_mp);
		self.state.monster = None;
		self.state.turn = 0;
		if self.state.round == self.data.balance.final_round {
			self.state.won = true;
			self.state.phase = Phase::Victory;
			events.push(Event::RunWon { round: self.state.round });
			return events;
		}
		self.state.phase = Phase::Merchant;
		events.extend(Merchant::new(&self.data, &mut self.rng, &mut self.state).enter());
		events
	}

	/// One rule for the three classes: chance(100) and chance(0) consume nothing (docs/game-design.md §8).
	fn drops(&mut self, monster: &MonsterInstance) -> Vec<Event> {
		let data = Rc::clone(&self.data);
		let row = data.balance.enemy_class(monster.enemy_class);
		let mut events = Vec::new();
		if self.rng.chance(row.drop_chance_pct) {
			for _ in 0..row.drops {
				events.extend(self.drop_item(row));
			}
		}
		if self.rng.chance(row.potion_drop_pct) {
			events.extend(self.drop_potion());
		}
		events
	}

	fn drop_item(&mut self, row: &EnemyClassDef) -> Vec<Event> {
		let data = &*self.data;
		let balance = &data.balance;
		let state = &mut self.state;
		let Some(item) = generate_item(
			data,
			&mut self.rng,
			data.vocation(&state.player.vocation_id),
			round_info(state.round, balance, data.tier_count()).tier,
			&row.rarity_weights,
			state.next_item_uid,
		) else {
			return Vec::new();
		};
		state.take_item_uid();
		let mut events =
			vec![Event::ItemDropped { uid: item.uid, item_id: item.item_id.clone(), rarity: item.rarity.clone() }];
		if state.player.bag.len() as i64 >= balance.bag_capacity {
			let value = item_value(&item, data);
			state.player.gold += value;
			events.push(Event::ItemAutoSold { uid: item.uid, item_id: item.item_id, gold: value });
		} else {
			state.player.bag.push(item);
		}
		events
	}

	fn drop_potion(&mut self) -> Vec<Event> {
		let state = &mut self.state;
		let unlocked: Vec<_> = self.data.potions.iter().filter(|potion| potion.unlock_round <= state.round).collect();
		if unlocked.is_empty() {
			return Vec::new();
		}
		let potion = *self.rng.pick(&unlocked);
		state.player.potions.insert(potion.id.clone(), state.player.potion_count(&potion.id) + 1);
		vec![Event::PotionDropped { potion_id: potion.id.clone() }]
	}

	fn end_run(&mut self) -> Vec<Event> {
		let state = &mut self.state;
		state.phase = Phase::GameOver;
		state.death_cause = None;
		vec![Event::RunEnded { won: state.won }]
	}

	fn continue_run(&mut self) -> Vec<Event> {
		self.state.phase = Phase::Merchant;
		Merchant::new(&self.data, &mut self.rng, &mut self.state).enter()
	}

	fn defeat(&mut self) -> Vec<Event> {
		let state = &mut self.state;
		let monster_id = state.monster.as_ref().map(|monster| monster.creature_id.clone()).unwrap_or_default();
		state.phase = Phase::GameOver;
		state.death_cause = Some(monster_id.clone());
		vec![Event::PlayerDied { monster_id, round: state.round }]
	}
}
