"""The game engine: a pure state machine `step(command) -> events` (docs/architecture.md)."""

from rpg.application.auto_equip import auto_equip
from rpg.application.battle import Battle, BattleOutcome
from rpg.application.commands import (
	Attack,
	BuyPotion,
	BuyStockItem,
	Cast,
	Command,
	ContinueRun,
	Defend,
	EndRun,
	Equip,
	NextFight,
	SellItem,
	Unequip,
	UsePotion,
)
from rpg.application.events import ErrorCode, Event, error, event
from rpg.application.loot import generate_item
from rpg.application.merchant import Merchant
from rpg.application.progression import Progression
from rpg.application.run_state import RunConfig, RunState
from rpg.application.spawner import spawn_monster
from rpg.domain.character import build_sheet, item_value
from rpg.domain.definitions import EnemyClassDef, GameData, UnknownIdError
from rpg.domain.entities import ItemInstance, MonsterInstance, Player
from rpg.domain.enums import Phase
from rpg.domain.formulas import round_info
from rpg.domain.rng import Rng


class GameEngine:
	def __init__(self, data: GameData, state: RunState, rng: Rng) -> None:
		self._data = data
		self._state = state
		self._rng = rng

	@property
	def state(self) -> RunState:
		return self._state

	@property
	def data(self) -> GameData:
		return self._data

	@property
	def rng_state(self) -> int:
		return self._rng.state

	@classmethod
	def new_run(cls, data: GameData, config: RunConfig, seed: int) -> tuple[GameEngine, list[Event]]:
		try:
			vocation = data.vocation(config.vocation_id)
			data.balance.difficulty(config.difficulty_id)
		except UnknownIdError as exc:
			raise ValueError(f"invalid run config: {exc}") from None

		player = Player(
			name=config.name,
			vocation_id=vocation.id,
			hp=vocation.start_hp,
			mp=vocation.start_mp,
			gold=data.balance.starting_gold,
			potions=dict(data.balance.starting_potions),
		)
		state = RunState(seed=seed, config=config, player=player)
		starter = data.item(vocation.starter_weapon)
		player.equipment[starter.slot] = ItemInstance(
			uid=state.take_item_uid(), item_id=starter.id, rarity="common", tier=starter.tier
		)
		engine = cls(data, state, Rng(seed))
		events: list[Event] = [
			event("run_started", seed=seed, vocation=vocation.id, difficulty=config.difficulty_id),
			*Merchant(data, engine._rng, state).enter(),
		]
		return engine, events

	@classmethod
	def restore(cls, data: GameData, state: RunState, rng_state: int) -> GameEngine:
		return cls(data, state, Rng(rng_state))

	def step(self, command: Command) -> list[Event]:
		events = self._dispatch(command)
		self._state.stats.record(events, self._state.round)
		return events

	def _dispatch(self, command: Command) -> list[Event]:
		phase = self._state.phase
		match command:
			case Attack() | Cast() | UsePotion() | Defend():
				if phase is not Phase.BATTLE:
					return [error(ErrorCode.INVALID_PHASE)]
				return self._battle_turn(command)
			case NextFight():
				if phase is not Phase.MERCHANT:
					return [error(ErrorCode.INVALID_PHASE)]
				return self._next_fight()
			case BuyPotion() | SellItem() | Equip() | Unequip() | BuyStockItem():
				if phase is not Phase.MERCHANT:
					return [error(ErrorCode.INVALID_PHASE)]
				return Merchant(self._data, self._rng, self._state).handle(command)
			case EndRun() | ContinueRun():
				if phase is not Phase.VICTORY:
					return [error(ErrorCode.INVALID_PHASE)]
				return self._end_run() if isinstance(command, EndRun) else self._continue_run()

	def _next_fight(self) -> list[Event]:
		state = self._state
		state.round += 1
		difficulty = self._data.balance.difficulty(state.config.difficulty_id)
		monster, info = spawn_monster(self._data, self._rng, state.round, difficulty)
		state.monster = monster
		state.phase = Phase.BATTLE
		state.turn = 1
		state.merchant_stock = []
		return [
			event(
				"round_started",
				round=state.round,
				tier=info.tier,
				cycle=info.cycle,
				monsterId=monster.creature_id,
				isBoss=monster.is_boss,
				enemyClass=monster.enemy_class,
				hp=monster.hp,
			)
		]

	def _battle_turn(self, command: Attack | Cast | UsePotion | Defend) -> list[Event]:
		battle = Battle(self._data, self._rng, self._state)
		invalid = battle.validate(command)
		if invalid is not None:
			return [invalid]
		events, outcome = battle.play_turn(command)
		if outcome == BattleOutcome.VICTORY:
			events.extend(self._victory())
		elif outcome == BattleOutcome.DEFEAT:
			events.extend(self._defeat())
		return events

	def _victory(self) -> list[Event]:
		state = self._state
		player = state.player
		monster = state.monster
		if monster is None:
			raise RuntimeError("victory without a monster")
		events: list[Event] = [
			event(
				"monster_killed",
				monsterId=monster.creature_id,
				isBoss=monster.is_boss,
				enemyClass=monster.enemy_class,
			)
		]
		events.extend(Progression(self._data).gain_experience(player, monster.xp))

		gold = self._rng.roll(monster.gold_min, monster.gold_max)
		player.gold += gold
		events.append(event("gold_looted", amount=gold))
		events.extend(self._drops(monster))
		if state.config.auto_equip:
			events.extend(auto_equip(state, self._data))

		player.statuses.clear()
		player.stun_cooldown = 0
		player.defending = False
		sheet = build_sheet(player, self._data)
		player.hp = min(player.hp, sheet.max_hp)
		player.mp = min(player.mp, sheet.max_mp)
		state.monster = None
		state.turn = 0
		if state.round == self._data.balance.final_round:
			state.won = True
			state.phase = Phase.VICTORY
			events.append(event("run_won", round=state.round))
			return events
		state.phase = Phase.MERCHANT
		events.extend(Merchant(self._data, self._rng, state).enter())
		return events

	def _drops(self, monster: MonsterInstance) -> list[Event]:
		"""One rule for the three classes: chance(100) and chance(0) consume nothing (docs/game-design.md §8)."""
		row = self._data.balance.enemy_class(monster.enemy_class)
		events: list[Event] = []
		if self._rng.chance(row.drop_chance_pct):
			for _ in range(row.drops):
				events.extend(self._drop_item(row))
		if self._rng.chance(row.potion_drop_pct):
			events.extend(self._drop_potion())
		return events

	def _drop_item(self, row: EnemyClassDef) -> list[Event]:
		state = self._state
		balance = self._data.balance
		item = generate_item(
			self._data,
			self._rng,
			vocation=self._data.vocation(state.player.vocation_id),
			tier=round_info(state.round, balance, self._data.tier_count).tier,
			weights=row.rarity_weights,
			uid=state.next_item_uid,
		)
		if item is None:
			return []
		state.take_item_uid()
		events: list[Event] = [event("item_dropped", uid=item.uid, itemId=item.item_id, rarity=item.rarity)]
		if len(state.player.bag) >= balance.bag_capacity:
			value = item_value(item, self._data)
			state.player.gold += value
			events.append(event("item_auto_sold", uid=item.uid, itemId=item.item_id, gold=value))
		else:
			state.player.bag.append(item)
		return events

	def _drop_potion(self) -> list[Event]:
		state = self._state
		unlocked = [potion for potion in self._data.potions if potion.unlock_round <= state.round]
		if not unlocked:
			return []
		potion = self._rng.pick(unlocked)
		state.player.potions[potion.id] = state.player.potion_count(potion.id) + 1
		return [event("potion_dropped", potionId=potion.id)]

	def _end_run(self) -> list[Event]:
		state = self._state
		state.phase = Phase.GAME_OVER
		state.death_cause = None
		return [event("run_ended", won=state.won)]

	def _continue_run(self) -> list[Event]:
		state = self._state
		state.phase = Phase.MERCHANT
		return Merchant(self._data, self._rng, state).enter()

	def _defeat(self) -> list[Event]:
		state = self._state
		monster = state.monster
		monster_id = "" if monster is None else monster.creature_id
		state.phase = Phase.GAME_OVER
		state.death_cause = monster_id
		return [event("player_died", monsterId=monster_id, round=state.round)]


__all__ = ["GameEngine", "RunConfig"]
