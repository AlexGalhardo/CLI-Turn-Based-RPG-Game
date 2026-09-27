"""Experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9)."""

from rpg.application.events import Event, event
from rpg.domain.character import build_sheet
from rpg.domain.definitions import GameData, SpellDef
from rpg.domain.entities import Player
from rpg.domain.formulas import mana_for_magic_level, spell_level_for_uses, xp_for_level


class Progression:
	def __init__(self, data: GameData) -> None:
		self._data = data

	def gain_experience(self, player: Player, amount: int) -> list[Event]:
		player.xp += amount
		events: list[Event] = [event("xp_gained", amount=amount, total=player.xp)]
		vocation = self._data.vocation(player.vocation_id)
		while player.xp >= xp_for_level(player.level + 1):
			player.level += 1
			sheet = build_sheet(player, self._data)
			player.hp = min(sheet.max_hp, player.hp + vocation.hp_per_level)
			player.mp = min(sheet.max_mp, player.mp + vocation.mp_per_level)
			events.append(event("level_up", level=player.level, maxHp=sheet.max_hp, maxMp=sheet.max_mp))
		return events

	def after_cast(self, player: Player, spell: SpellDef, mana_cost: int) -> list[Event]:
		levels = self._data.balance.spell_levels
		events: list[Event] = []
		uses_before = player.spell_uses.get(spell.id, 0)
		player.spell_uses[spell.id] = uses_before + 1
		before = spell_level_for_uses(uses_before, levels)
		after = spell_level_for_uses(uses_before + 1, levels)
		if after.level != before.level:
			events.append(event("spell_level_up", spellId=spell.id, level=after.level))

		player.mana_spent += mana_cost
		while player.mana_spent >= mana_for_magic_level(player.magic_level, self._data.balance):
			player.magic_level += 1
			events.append(event("magic_level_up", magicLevel=player.magic_level))
		return events
