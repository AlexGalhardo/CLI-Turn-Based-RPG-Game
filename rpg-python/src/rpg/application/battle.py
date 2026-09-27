"""Battle resolution, following docs/game-design.md §6. Every RNG call here is part of the contract."""

from rpg.application.commands import Attack, Cast, Defend, UsePotion
from rpg.application.events import ErrorCode, Event, error, event
from rpg.application.progression import Progression
from rpg.application.run_state import RunState
from rpg.domain.character import CharacterSheet, build_sheet
from rpg.domain.definitions import GameData, MonsterAttack, SpellDef
from rpg.domain.entities import ActiveStatus, MonsterInstance, Player
from rpg.domain.enums import Element, Resource, SpellKind, StatusKind, Target
from rpg.domain.formulas import armor_mitigation, pct, spell_level_for_uses
from rpg.domain.rng import Rng

STUN = "stun"
STUN_COOLDOWN_TURNS = 2

type BattleCommand = Attack | Cast | UsePotion | Defend


class BattleOutcome:
	ONGOING = "ongoing"
	VICTORY = "victory"
	DEFEAT = "defeat"


class Battle:
	def __init__(self, data: GameData, rng: Rng, state: RunState) -> None:
		self._data = data
		self._rng = rng
		self._state = state
		self._progression = Progression(data)

	@property
	def _player(self) -> Player:
		return self._state.player

	@property
	def _monster(self) -> MonsterInstance:
		monster = self._state.monster
		if monster is None:
			raise RuntimeError("battle without a monster")
		return monster

	def _sheet(self) -> CharacterSheet:
		return build_sheet(self._player, self._data)

	# ── validation ────────────────────────────────────────────────────────────

	def validate(self, command: BattleCommand) -> Event | None:
		"""Returns an error event for an invalid command. Validation never consumes randomness."""
		match command:
			case Cast(spell_id):
				vocation = self._data.vocation(self._player.vocation_id)
				if spell_id not in vocation.spells:
					return error(ErrorCode.UNKNOWN_SPELL)
				if self._player.mp < self._spell_cost(self._data.spell(spell_id)):
					return error(ErrorCode.NOT_ENOUGH_MANA)
			case UsePotion(potion_id):
				if potion_id not in {potion.id for potion in self._data.potions}:
					return error(ErrorCode.UNKNOWN_POTION)
				if self._player.potion_count(potion_id) <= 0:
					return error(ErrorCode.NO_POTION)
			case Attack() | Defend():
				pass
		return None

	def _spell_cost(self, spell: SpellDef) -> int:
		level = spell_level_for_uses(self._player.spell_uses.get(spell.id, 0), self._data.balance.spell_levels)
		return pct(spell.mana, level.mana_pct)

	# ── turn ──────────────────────────────────────────────────────────────────

	def play_turn(self, command: BattleCommand) -> tuple[list[Event], str]:
		events: list[Event] = []
		self._player_action(command, events)
		if self._monster.hp <= 0:
			return events, BattleOutcome.VICTORY
		while True:
			if self._monster_phase(events):
				return events, BattleOutcome.VICTORY
			if self._player.hp <= 0:
				return events, BattleOutcome.DEFEAT
			if self._end_of_turn(events):
				return events, BattleOutcome.DEFEAT
			if not self._consume_stun(self._player.statuses):
				return events, BattleOutcome.ONGOING
			self._player.stun_cooldown = STUN_COOLDOWN_TURNS
			events.append(event("player_stunned"))

	# ── step 1: player action ─────────────────────────────────────────────────

	def _player_action(self, command: BattleCommand, events: list[Event]) -> None:
		match command:
			case Attack():
				self._melee(events)
			case Cast(spell_id):
				self._cast(self._data.spell(spell_id), events)
			case UsePotion(potion_id):
				self._drink(potion_id, events)
			case Defend():
				self._player.defending = True
				events.append(event("player_defended"))

	def _melee(self, events: list[Event]) -> None:
		sheet = self._sheet()
		damage = pct(self._rng.roll(sheet.melee_min, sheet.melee_max), 100 + sheet.physical_damage)
		damage, crit = self._roll_crit(damage, sheet)
		damage = self._resisted(damage, sheet.weapon_element)
		self._hit_monster(damage)
		events.append(event("player_attacked", damage=damage, crit=crit, element=sheet.weapon_element.value))
		self._leech(damage, sheet, events)

	def _cast(self, spell: SpellDef, events: list[Event]) -> None:
		player = self._player
		sheet = self._sheet()
		level = spell_level_for_uses(player.spell_uses.get(spell.id, 0), self._data.balance.spell_levels)
		cost = pct(spell.mana, level.mana_pct)
		player.mp -= cost
		bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
		amount = pct(
			pct(self._rng.roll(spell.min + bonus, spell.max + bonus), level.effect_pct), 100 + sheet.spell_power
		)

		if spell.kind is SpellKind.ATTACK:
			damage, crit = self._roll_crit(amount, sheet)
			damage = self._resisted(damage, spell.element)
			self._hit_monster(damage)
			events.append(
				event("spell_cast", spellId=spell.id, damage=damage, crit=crit, element=spell.element.value, mana=cost)
			)
			self._leech(damage, sheet, events)
			bonus_effect = spell.level3_bonus
			if level.level == 3 and bonus_effect.status is not None and self._rng.chance(bonus_effect.chance):
				per_turn = max(1, pct(damage, self._data.balance.spell_status_damage_pct))
				self._apply_status(Target.MONSTER, bonus_effect.status, per_turn, events)
		else:
			healed = min(amount, sheet.max_hp - player.hp)
			player.hp += healed
			events.append(event("spell_healed", spellId=spell.id, amount=healed, mana=cost))
			if level.level == 3 and spell.level3_bonus.cleanse:
				for status in list(player.statuses):
					player.statuses.remove(status)
					events.append(event("status_expired", target=Target.PLAYER.value, status=status.status_id))

		events.extend(self._progression.after_cast(player, spell, cost))

	def _drink(self, potion_id: str, events: list[Event]) -> None:
		player = self._player
		sheet = self._sheet()
		potion = self._data.potion(potion_id)
		player.potions[potion_id] -= 1
		amount = self._rng.roll(potion.min, potion.max)
		if potion.resource is Resource.HP:
			restored = min(amount, sheet.max_hp - player.hp)
			player.hp += restored
		else:
			restored = min(amount, sheet.max_mp - player.mp)
			player.mp += restored
		events.append(event("potion_used", potionId=potion_id, amount=restored, resource=potion.resource.value))

	def _roll_crit(self, damage: int, sheet: CharacterSheet) -> tuple[int, bool]:
		if self._rng.chance(sheet.crit_chance):
			return pct(damage, self._data.balance.crit_multiplier_pct + sheet.crit_damage), True
		return damage, False

	def _resisted(self, damage: int, element: Element) -> int:
		resistance = self._data.creature(self._monster.creature_id).resistance(element)
		if resistance == 0:
			return 0
		return max(1, pct(damage, resistance))

	def _hit_monster(self, damage: int) -> None:
		self._monster.hp = max(0, self._monster.hp - damage)

	def _leech(self, damage: int, sheet: CharacterSheet, events: list[Event]) -> None:
		player = self._player
		hp_gain = min(pct(damage, sheet.life_leech), sheet.max_hp - player.hp)
		mp_gain = min(pct(damage, sheet.mana_leech), sheet.max_mp - player.mp)
		if hp_gain <= 0 and mp_gain <= 0:
			return
		player.hp += max(0, hp_gain)
		player.mp += max(0, mp_gain)
		events.append(event("leeched", hp=max(0, hp_gain), mp=max(0, mp_gain)))

	# ── step 3: monster phase ─────────────────────────────────────────────────

	def _monster_phase(self, events: list[Event]) -> bool:
		"""Returns True when the monster died from its own status ticks."""
		monster = self._monster
		self._tick(Target.MONSTER, monster.statuses, events)
		if monster.hp <= 0:
			return True
		if self._consume_stun(monster.statuses):
			monster.stun_cooldown = STUN_COOLDOWN_TURNS
			events.append(event("monster_stunned"))
			return False

		if monster.is_boss:
			every = self._data.balance.boss_telegraph_every
			position = monster.boss_actions % (every + 1)
			monster.boss_actions += 1
			charge_id = self._data.creature(monster.creature_id).charge_attack
			if charge_id is not None and position == every - 1:
				charge = monster.attack(charge_id)
				events.append(event("boss_telegraph", attackId=charge.id, element=charge.element.value))
				return False
			if charge_id is not None and position == every:
				self._resolve_monster_attack(monster.attack(charge_id), True, events)
				return False

		index = self._rng.weighted([attack.weight for attack in monster.attacks])
		self._resolve_monster_attack(monster.attacks[index], False, events)
		return False

	def _resolve_monster_attack(self, attack: MonsterAttack, charged: bool, events: list[Event]) -> None:
		player = self._player
		sheet = self._sheet()
		if self._rng.chance(sheet.dodge):
			events.append(event("attack_dodged", attackId=attack.id))
			return
		if attack.element is Element.PHYSICAL and self._rng.chance(sheet.parry):
			events.append(event("attack_parried", attackId=attack.id))
			return
		damage = self._rng.roll(attack.min, attack.max)
		if charged:
			damage = pct(damage, self._data.balance.boss_charge_damage_pct)
		if attack.element is Element.PHYSICAL:
			damage = armor_mitigation(damage, sheet.armor)
		damage = pct(damage, 100 - sheet.protection(attack.element))
		if player.defending:
			damage = pct(damage, self._data.balance.defend_damage_pct)
		damage = max(1, damage)
		player.hp = max(0, player.hp - damage)
		events.append(
			event("monster_attacked", attackId=attack.id, damage=damage, element=attack.element.value, charged=charged)
		)
		if attack.status is not None and self._rng.chance(attack.status.chance):
			per_turn = max(1, pct(damage, attack.status.damage_pct))
			self._apply_status(Target.PLAYER, attack.status.status, per_turn, events)

	# ── step 5: end of turn ───────────────────────────────────────────────────

	def _end_of_turn(self, events: list[Event]) -> bool:
		"""Returns True when the player died from status ticks."""
		player = self._player
		self._tick(Target.PLAYER, player.statuses, events)
		if player.hp <= 0:
			return True
		sheet = self._sheet()
		hp_gain = max(0, min(sheet.hp_regen, sheet.max_hp - player.hp))
		mp_gain = max(0, min(sheet.mp_regen, sheet.max_mp - player.mp))
		player.hp += hp_gain
		player.mp += mp_gain
		if hp_gain > 0 or mp_gain > 0:
			events.append(event("regenerated", hp=hp_gain, mp=mp_gain))
		player.defending = False
		player.stun_cooldown = max(0, player.stun_cooldown - 1)
		self._monster.stun_cooldown = max(0, self._monster.stun_cooldown - 1)
		self._state.turn += 1
		return False

	# ── statuses ──────────────────────────────────────────────────────────────

	def _apply_status(self, target: Target, status_id: str, per_turn: int, events: list[Event]) -> None:
		definition = self._data.status(status_id)
		if target is Target.PLAYER:
			statuses, cooldown = self._player.statuses, self._player.stun_cooldown
		else:
			statuses, cooldown = self._monster.statuses, self._monster.stun_cooldown
			if self._data.creature(self._monster.creature_id).resistance(definition.element) == 0:
				return

		if definition.kind is StatusKind.STUN:
			if cooldown > 0 or any(s.status_id == STUN for s in statuses):
				return
			statuses.append(ActiveStatus(STUN, definition.turns, 0))
			events.append(event("status_applied", target=target.value, status=STUN, turns=definition.turns, perTurn=0))
			return

		existing = next((s for s in statuses if s.status_id == status_id), None)
		if existing is None:
			existing = ActiveStatus(status_id, definition.turns, per_turn)
			statuses.append(existing)
		else:
			existing.turns = definition.turns
			existing.per_turn = max(existing.per_turn, per_turn)
		events.append(
			event(
				"status_applied",
				target=target.value,
				status=status_id,
				turns=existing.turns,
				perTurn=existing.per_turn,
			)
		)

	def _tick(self, target: Target, statuses: list[ActiveStatus], events: list[Event]) -> None:
		for status in list(statuses):
			definition = self._data.status(status.status_id)
			if definition.kind is not StatusKind.DOT:
				continue
			damage = self._status_damage(target, status.per_turn, definition.element)
			if target is Target.PLAYER:
				self._player.hp = max(0, self._player.hp - damage)
			else:
				self._hit_monster(damage)
			events.append(event("status_ticked", target=target.value, status=status.status_id, damage=damage))
			status.turns -= 1
			if status.turns <= 0:
				statuses.remove(status)
				events.append(event("status_expired", target=target.value, status=status.status_id))

	def _status_damage(self, target: Target, per_turn: int, element: Element) -> int:
		if target is Target.PLAYER:
			return max(1, pct(per_turn, 100 - self._sheet().protection(element)))
		resistance = self._data.creature(self._monster.creature_id).resistance(element)
		return 0 if resistance == 0 else max(1, pct(per_turn, resistance))

	@staticmethod
	def _consume_stun(statuses: list[ActiveStatus]) -> bool:
		for status in statuses:
			if status.status_id == STUN:
				statuses.remove(status)
				return True
		return False
