"""Auto-battle policy (docs/game-design.md §13): picks the player's battle commands from the run state only.

It lives in the application layer, not in the engine: the commands it returns are ordinary commands, so a fight
played by the policy replays like any other. Every command it returns is valid (affordable spells, owned potions).
"""

from enum import StrEnum

from rpg.application.battle import BattleCommand
from rpg.application.commands import Attack, Cast, Defend, UsePotion
from rpg.application.run_state import RunState
from rpg.domain.character import build_sheet
from rpg.domain.definitions import GameData, PotionDef, SpellDef
from rpg.domain.enums import Resource, SpellKind
from rpg.domain.formulas import pct, spell_level_for_uses

OFFENSE_ATTACK = "attack"


class AutoBattleMode(StrEnum):
	MELEE = "melee"
	SPELLS = "spells"
	BALANCED = "balanced"


def _strongest[T: SpellDef | PotionDef](options: list[T]) -> T | None:
	"""Highest `max`; ties go to the lowest id."""
	return min(options, key=lambda option: (-option.max, option.id)) if options else None


class AutoBattlePolicy:
	def __init__(self, data: GameData, mode: AutoBattleMode) -> None:
		self._data = data
		self._mode = data.balance.auto_battle.mode(mode.value)
		self._config = data.balance.auto_battle

	def choose(self, state: RunState) -> BattleCommand:
		player = state.player
		sheet = build_sheet(player, self._data)
		config = self._config

		if player.hp * 100 < sheet.max_hp * config.emergency_heal_below_pct:
			heal = self._heal(state)
			if heal is not None:
				return heal
		every = self._mode.support_every
		if state.turn % every == every - 1:
			if player.hp * 100 < sheet.max_hp * config.heal_below_pct:
				heal = self._heal(state)
				if heal is not None:
					return heal
			if player.mp * 100 < sheet.max_mp * config.mana_below_pct:
				potion = self._best_potion(state, Resource.MP)
				if potion is not None:
					return UsePotion(potion.id)
			if self._telegraph_pending(state):
				return Defend()
		return self._offense(state)

	def _offense(self, state: RunState) -> BattleCommand:
		if self._mode.offense == OFFENSE_ATTACK:
			return Attack()
		spell = _strongest(self._affordable(state, SpellKind.ATTACK))
		return Attack() if spell is None else Cast(spell.id)

	def _heal(self, state: RunState) -> BattleCommand | None:
		spell = _strongest(self._affordable(state, SpellKind.HEAL))
		if spell is not None:
			return Cast(spell.id)
		potion = self._best_potion(state, Resource.HP)
		return None if potion is None else UsePotion(potion.id)

	def _affordable(self, state: RunState, kind: SpellKind) -> list[SpellDef]:
		player = state.player
		levels = self._data.balance.spell_levels
		spells = [self._data.spell(spell_id) for spell_id in self._data.vocation(player.vocation_id).spells]
		return [
			spell
			for spell in spells
			if spell.kind is kind
			and pct(spell.mana, spell_level_for_uses(player.spell_uses.get(spell.id, 0), levels).mana_pct) <= player.mp
		]

	def _best_potion(self, state: RunState, resource: Resource) -> PotionDef | None:
		return _strongest(
			[p for p in self._data.potions if p.resource is resource and state.player.potion_count(p.id) > 0]
		)

	def _telegraph_pending(self, state: RunState) -> bool:
		"""The boss announced its charged attack: its next action is the charge."""
		monster = state.monster
		if monster is None or not monster.is_boss:
			return False
		every = self._data.balance.boss_telegraph_every
		return monster.boss_actions % (every + 1) == every
