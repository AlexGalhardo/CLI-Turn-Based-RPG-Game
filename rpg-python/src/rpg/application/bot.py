"""A deterministic heuristic player used by the simulator and by the end-to-end parity tests.

The bot only reads the run state and returns one command at a time, so it can drive any engine implementation.
Its decisions are part of the golden "bot full run" files: changing them requires regenerating those files.
"""

from rpg.application.commands import (
	Attack,
	BuyPotion,
	Cast,
	Command,
	Defend,
	EndRun,
	Equip,
	NextFight,
	SellItem,
	UsePotion,
)
from rpg.application.loot import can_use
from rpg.application.merchant import available_potions
from rpg.application.run_state import RunState
from rpg.domain.character import build_sheet, item_score, required_level
from rpg.domain.definitions import GameData, PotionDef, SpellDef
from rpg.domain.entities import MonsterInstance
from rpg.domain.enums import Phase, Resource, SpellKind
from rpg.domain.formulas import pct, spell_level_for_uses

HEAL_THRESHOLD_PCT = 45
MANA_POTION_THRESHOLD_PCT = 25
MAX_POTION_STOCK = 20


class GreedyBot:
	def __init__(self, data: GameData) -> None:
		self._data = data

	def choose(self, state: RunState) -> Command:
		if state.phase is Phase.BATTLE:
			return self._battle(state)
		if state.phase is Phase.VICTORY:
			return EndRun()
		return self._merchant(state)

	# ── battle ────────────────────────────────────────────────────────────────

	def _battle(self, state: RunState) -> Command:
		player = state.player
		monster = state.monster
		if monster is None:
			raise RuntimeError("battle without a monster")
		sheet = build_sheet(player, self._data)

		if self._charge_incoming(monster):
			return Defend()
		if player.hp * 100 < sheet.max_hp * HEAL_THRESHOLD_PCT:
			heal = self._heal(state)
			if heal is not None:
				return heal
		if player.mp * 100 < sheet.max_mp * MANA_POTION_THRESHOLD_PCT:
			potion = self._best_owned_potion(state, Resource.MP)
			if potion is not None:
				return UsePotion(potion.id)
		spell = self._best_attack_spell(state, monster)
		if spell is not None:
			return Cast(spell.id)
		return Attack()

	def _charge_incoming(self, monster: MonsterInstance) -> bool:
		if not monster.is_boss:
			return False
		every = self._data.balance.boss_telegraph_every
		return monster.boss_actions % (every + 1) == every

	def _cost(self, state: RunState, spell: SpellDef) -> int:
		uses = state.player.spell_uses.get(spell.id, 0)
		return pct(spell.mana, spell_level_for_uses(uses, self._data.balance.spell_levels).mana_pct)

	def _spells(self, state: RunState, kind: SpellKind) -> list[SpellDef]:
		vocation = self._data.vocation(state.player.vocation_id)
		spells = [self._data.spell(spell_id) for spell_id in vocation.spells]
		return [s for s in spells if s.kind is kind and self._cost(state, s) <= state.player.mp]

	def _heal(self, state: RunState) -> Command | None:
		spells = self._spells(state, SpellKind.HEAL)
		if spells:
			return Cast(max(spells, key=lambda s: (s.max, s.id)).id)
		potion = self._best_owned_potion(state, Resource.HP)
		return None if potion is None else UsePotion(potion.id)

	def _best_owned_potion(self, state: RunState, resource: Resource) -> PotionDef | None:
		owned = [p for p in self._data.potions if p.resource is resource and state.player.potion_count(p.id) > 0]
		return max(owned, key=lambda p: (p.max, p.id)) if owned else None

	def _best_attack_spell(self, state: RunState, monster: MonsterInstance) -> SpellDef | None:
		creature = self._data.creature(monster.creature_id)
		candidates = [s for s in self._spells(state, SpellKind.ATTACK) if creature.resistance(s.element) > 0]
		if not candidates:
			return None
		return max(candidates, key=lambda s: ((s.min + s.max) * creature.resistance(s.element), s.id))

	# ── merchant ──────────────────────────────────────────────────────────────

	def _merchant(self, state: RunState) -> Command:
		player = state.player
		vocation = self._data.vocation(player.vocation_id)
		for item in sorted(player.bag, key=lambda i: i.uid):
			definition = self._data.item(item.item_id)
			if not can_use(definition, vocation) or required_level(item, self._data) > player.level:
				continue
			current = player.equipment.get(definition.slot)
			if current is None or item_score(item, self._data) > item_score(current, self._data):
				return Equip(item.uid)
		if player.bag:
			return SellItem(min(player.bag, key=lambda i: i.uid).uid)
		purchase = self._potion_purchase(state, Resource.HP) or self._potion_purchase(state, Resource.MP)
		return purchase or NextFight()

	def _potion_purchase(self, state: RunState, resource: Resource) -> Command | None:
		unlocked = set(available_potions(state, self._data))
		options = [p for p in self._data.potions if p.resource is resource and p.id in unlocked]
		if not options:
			return None
		best = max(options, key=lambda p: (p.max, p.id))
		owned = sum(state.player.potion_count(p.id) for p in options)
		target = min(MAX_POTION_STOCK, 5 + state.round // 5)
		budget = state.player.gold // 2 if resource is Resource.MP else state.player.gold
		quantity = min(target - owned, budget // best.price)
		if quantity <= 0:
			return None
		return BuyPotion(best.id, quantity)
