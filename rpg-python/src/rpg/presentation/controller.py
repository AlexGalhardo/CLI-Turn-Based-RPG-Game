"""Framework-independent UI state machine: which screen is shown, its options and what each key does.

The Textual app only renders this controller. The TypeScript (Ink) and Go (Bubble Tea) ports implement the same
controller, which is what keeps the three interfaces practically identical (docs/tui.md).
"""

import secrets
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

from rpg.application.commands import (
	Attack,
	BuyPotion,
	BuyStockItem,
	Cast,
	Command,
	Defend,
	Equip,
	NextFight,
	SellItem,
	Unequip,
	UsePotion,
)
from rpg.application.game_session import GameSession, Repositories, StepResult
from rpg.application.loot import can_use
from rpg.application.merchant import available_potions, stock_price
from rpg.application.ports import Clock
from rpg.application.profile import ProfileService
from rpg.application.run_state import RunConfig
from rpg.domain.character import build_sheet, item_value
from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance
from rpg.domain.enums import Element, Phase, Slot, SpellKind
from rpg.domain.formulas import mana_for_magic_level, pct, round_info, spell_level_for_uses, xp_for_level
from rpg.infrastructure.i18n import DEFAULT_LOCALE, SUPPORTED_LOCALES, Translator
from rpg.infrastructure.repositories import Settings, SettingsRepository
from rpg.presentation.event_text import EventFormatter
from rpg.presentation.render import list_key

MAX_LOG_LINES = 50
MAX_NAME_LENGTH = 16
MAX_QUANTITY_DIGITS = 2
PAGE_SIZE = 10


class View(StrEnum):
	LANGUAGE = "language"
	TITLE = "title"
	DIFFICULTY = "difficulty"
	NAME = "name"
	VOCATION = "vocation"
	MERCHANT = "merchant"
	BUY_POTIONS = "buy_potions"
	QUANTITY = "quantity"
	SELL = "sell"
	EQUIPMENT = "equipment"
	STOCK = "stock"
	CHARACTER = "character"
	BATTLE = "battle"
	SPELLS = "spells"
	POTIONS = "potions"
	GAME_OVER = "game_over"
	HALL_OF_FAME = "hall_of_fame"
	BESTIARY = "bestiary"
	ACHIEVEMENTS = "achievements"


BATTLE_VIEWS = {View.BATTLE, View.SPELLS, View.POTIONS}
TEXT_INPUT_VIEWS = {View.NAME, View.QUANTITY}
PAGED_VIEWS = {View.HALL_OF_FAME, View.BESTIARY, View.ACHIEVEMENTS, View.CHARACTER}


@dataclass(frozen=True, slots=True)
class MenuOption:
	key: str
	label: str
	color: str | None = None


@dataclass(frozen=True, slots=True)
class Services:
	data: GameData
	shared_dir: Path
	settings: SettingsRepository
	repositories: Repositories
	clock: Clock
	version: str


@dataclass(frozen=True, slots=True)
class MonsterView:
	name: str
	creature_id: str
	hp: int
	max_hp: int
	is_boss: bool
	element: str
	details: str


@dataclass(frozen=True, slots=True)
class PlayerView:
	summary: str
	gold: str
	hp: int
	max_hp: int
	mp: int
	max_mp: int
	xp: str
	statuses: str


type Action = Callable[[], None]


def random_seed() -> int:
	return secrets.randbelow(2**32)


class Controller:
	def __init__(
		self,
		services: Services,
		*,
		seed: int | None = None,
		locale_override: str | None = None,
		seed_source: Callable[[], int] = random_seed,
	) -> None:
		self._services = services
		self._data = services.data
		self._seed = seed
		self._seed_source = seed_source
		settings = services.settings.load()
		self.locale = locale_override or settings.locale or DEFAULT_LOCALE
		self._set_locale(self.locale)
		self.view = View.TITLE if (locale_override or settings.locale) else View.LANGUAGE
		self._language_return = View.TITLE
		self.session: GameSession | None = None
		self.log: deque[str] = deque(maxlen=MAX_LOG_LINES)
		self.message = ""
		self.input_buffer = ""
		self.exit_requested = False
		self.animation_cues: list[str] = []
		self.page = 0
		self._difficulty = "normal"
		self._name = ""
		self._potion_id = ""

	@property
	def services(self) -> Services:
		return self._services

	# ── i18n ──────────────────────────────────────────────────────────────────

	def _set_locale(self, locale: str) -> None:
		self.locale = locale
		self._translator = Translator(self._services.shared_dir, locale)
		self._formatter = EventFormatter(self._data, self._translator)

	def t(self, key: str, **params: object) -> str:
		return self._translator.t(key, **params)

	# ── queries used by renderers ─────────────────────────────────────────────

	def title(self) -> str:
		match self.view:
			case View.LANGUAGE:
				return self.t("language.title")
			case View.TITLE:
				return self.t("app.title")
			case View.DIFFICULTY:
				return self.t("new_run.difficulty")
			case View.NAME:
				return self.t("new_run.name")
			case View.VOCATION:
				return self.t("new_run.vocation")
			case View.MERCHANT:
				round_number = self.session.state.round if self.session else 0
				if round_number == 0:
					return self.t("merchant.title_start")
				return self.t("merchant.title", round=round_number)
			case View.BUY_POTIONS:
				return self.t("merchant.buy_potions")
			case View.QUANTITY:
				return self.t("merchant.quantity", name=self._data.potion(self._potion_id).name)
			case View.SELL:
				return self.t("merchant.sell_items")
			case View.EQUIPMENT:
				return self.t("merchant.equipment")
			case View.STOCK:
				return self.t("merchant.stock")
			case View.CHARACTER:
				return self.t("merchant.character")
			case View.BATTLE:
				return self.t("battle.title")
			case View.SPELLS:
				return self.t("battle.spells")
			case View.POTIONS:
				return self.t("battle.potions")
			case View.GAME_OVER:
				return self.t("gameover.title")
			case View.HALL_OF_FAME:
				return self.t("menu.hall_of_fame")
			case View.BESTIARY:
				return self.t("menu.bestiary")
			case View.ACHIEVEMENTS:
				return self.t("menu.achievements")

	def options(self) -> list[MenuOption]:
		return [option for option, _ in self._menu()]

	def body_lines(self) -> list[str]:
		"""Informative lines shown above the options (paged for long lists)."""
		lines = self._body()
		if self.view not in PAGED_VIEWS or len(lines) <= PAGE_SIZE:
			return lines
		pages = (len(lines) + PAGE_SIZE - 1) // PAGE_SIZE
		self.page = min(self.page, pages - 1)
		start = self.page * PAGE_SIZE
		return [*lines[start : start + PAGE_SIZE], "", self.t("menu.page", page=self.page + 1, pages=pages)]

	def input_prompt(self) -> str | None:
		if self.view in TEXT_INPUT_VIEWS:
			return f"> {self.input_buffer}_"
		return None

	def header(self) -> str:
		if self.session is None:
			return self.t("app.subtitle")
		state = self.session.state
		tier = round_info(max(1, state.round), self._data.balance, self._data.tier_count).tier + 1
		difficulty = self.t(f"difficulty.{state.config.difficulty_id}")
		round_text = self.t("hud.round", round=state.round, tier=tier, difficulty=difficulty)
		return f"{round_text} · {self.t('hud.seed', seed=state.seed)}"

	def monster_view(self) -> MonsterView | None:
		if self.session is None or self.session.state.monster is None:
			return None
		monster = self.session.state.monster
		creature = self._data.creature(monster.creature_id)
		main_element = max(monster.attacks, key=lambda a: (a.weight, a.id)).element
		weak = [self.t(f"element.{e.value}") for e in Element if creature.resistance(e) > 100]
		parts = [self.t(f"element.{a.element.value}") for a in monster.attacks]
		details = " · ".join(dict.fromkeys(parts))
		if weak:
			details += " · " + self.t("hud.weak", elements=", ".join(weak))
		statuses = [f"{self.t(f'status.{s.status_id}')}({s.turns})" for s in monster.statuses]
		if statuses:
			details += " · " + " ".join(statuses)
		return MonsterView(
			name=creature.name,
			creature_id=creature.id,
			hp=monster.hp,
			max_hp=monster.max_hp,
			is_boss=monster.is_boss,
			element=main_element.value,
			details=details,
		)

	def player_view(self) -> PlayerView | None:
		if self.session is None:
			return None
		player = self.session.state.player
		sheet = build_sheet(player, self._data)
		summary = self.t(
			"hud.player",
			name=player.name,
			vocation=self.t(f"vocation.{player.vocation_id}"),
			level=player.level,
			magicLevel=player.magic_level,
		)
		statuses = " ".join(f"{self.t(f'status.{s.status_id}')}({s.turns})" for s in player.statuses)
		return PlayerView(
			summary=summary,
			gold=self.t("hud.gold", gold=player.gold),
			hp=player.hp,
			max_hp=sheet.max_hp,
			mp=player.mp,
			max_mp=sheet.max_mp,
			xp=self.t("hud.xp", xp=player.xp, next=xp_for_level(player.level + 1)),
			statuses=statuses,
		)

	# ── input ─────────────────────────────────────────────────────────────────

	def press(self, key: str) -> None:
		self.message = ""
		if self.view in TEXT_INPUT_VIEWS:
			self._text_input(key)
			return
		if self.view in PAGED_VIEWS and key in {"n", "p"}:
			self.page = max(0, self.page + (1 if key == "n" else -1))
			return
		if key == "escape":
			key = "0"
		for option, action in self._menu():
			if option.key == key:
				action()
				return

	def _text_input(self, key: str) -> None:
		if key == "escape":
			self.input_buffer = ""
			self.view = View.DIFFICULTY if self.view is View.NAME else View.BUY_POTIONS
		elif key == "backspace":
			self.input_buffer = self.input_buffer[:-1]
		elif key == "enter":
			self._submit_text()
		elif self.view is View.NAME and len(key) == 1 and key.isprintable():
			if len(self.input_buffer) < MAX_NAME_LENGTH:
				self.input_buffer += key
		elif self.view is View.QUANTITY and key.isdigit() and len(self.input_buffer) < MAX_QUANTITY_DIGITS:
			self.input_buffer += key

	def _submit_text(self) -> None:
		text = self.input_buffer.strip()
		self.input_buffer = ""
		if self.view is View.NAME:
			if not 1 <= len(text) <= MAX_NAME_LENGTH:
				self.message = self.t("new_run.name_invalid")
				return
			self._name = text
			self.view = View.VOCATION
			return
		self.view = View.BUY_POTIONS
		if text and int(text) > 0:
			self._step(BuyPotion(self._potion_id, int(text)))

	# ── menus ─────────────────────────────────────────────────────────────────

	def _menu(self) -> list[tuple[MenuOption, Action]]:
		match self.view:
			case View.LANGUAGE:
				return [
					(MenuOption(str(i + 1), self.t(f"language.{locale}")), self._choose_language(locale))
					for i, locale in enumerate(SUPPORTED_LOCALES)
				]
			case View.TITLE:
				return self._title_menu()
			case View.DIFFICULTY:
				items = [
					(
						MenuOption(
							str(i + 1),
							f"{self.t(f'difficulty.{d.id}')} — {self.t(f'difficulty.{d.id}.description')}",
						),
						self._choose_difficulty(d.id),
					)
					for i, d in enumerate(self._data.balance.difficulties)
				]
				return [*items, self._back(View.TITLE)]
			case View.VOCATION:
				items = [
					(
						MenuOption(
							str(i + 1), f"{self.t(f'vocation.{v.id}')} — {self.t(f'vocation.{v.id}.description')}"
						),
						self._choose_vocation(v.id),
					)
					for i, v in enumerate(self._data.vocations)
				]
				return [*items, self._back(View.DIFFICULTY)]
			case View.MERCHANT:
				return self._merchant_menu()
			case View.BUY_POTIONS:
				return [*self._potion_shop(), self._back(View.MERCHANT)]
			case View.SELL:
				return [*self._sell_menu(), self._back(View.MERCHANT)]
			case View.EQUIPMENT:
				return [*self._equipment_menu(), self._back(View.MERCHANT)]
			case View.STOCK:
				return [*self._stock_menu(), self._back(View.MERCHANT)]
			case View.CHARACTER:
				return [self._back(View.MERCHANT)]
			case View.BATTLE:
				return [
					(MenuOption("1", self.t("battle.attack")), lambda: self._step(Attack())),
					(MenuOption("2", self.t("battle.spells")), self._go(View.SPELLS)),
					(MenuOption("3", self.t("battle.potions")), self._go(View.POTIONS)),
					(MenuOption("4", self.t("battle.defend")), lambda: self._step(Defend())),
					(MenuOption("q", self.t("battle.save_quit")), self._save_and_quit),
				]
			case View.SPELLS:
				return [*self._spell_menu(), self._back(View.BATTLE)]
			case View.POTIONS:
				return [*self._battle_potions(), self._back(View.BATTLE)]
			case View.GAME_OVER:
				return [
					(MenuOption("1", self.t("gameover.new_run")), self._new_run),
					(MenuOption("2", self.t("gameover.title_screen")), self._go(View.TITLE)),
				]
			case View.HALL_OF_FAME | View.BESTIARY | View.ACHIEVEMENTS:
				return [self._back(View.TITLE)]
			case View.NAME | View.QUANTITY:
				return []

	def _back(self, target: View) -> tuple[MenuOption, Action]:
		return MenuOption("0", self.t("menu.back")), self._go(target)

	def _go(self, target: View) -> Action:
		def action() -> None:
			self.view = target
			self.page = 0

		return action

	def _title_menu(self) -> list[tuple[MenuOption, Action]]:
		menu: list[tuple[MenuOption, Action]] = []
		if self._services.repositories.saves.load() is not None:
			menu.append((MenuOption("1", self.t("menu.continue")), self._continue))
		menu += [
			(MenuOption("2", self.t("menu.new_run")), self._new_run),
			(MenuOption("3", self.t("menu.hall_of_fame")), self._go(View.HALL_OF_FAME)),
			(MenuOption("4", self.t("menu.bestiary")), self._go(View.BESTIARY)),
			(MenuOption("5", self.t("menu.achievements")), self._go(View.ACHIEVEMENTS)),
			(MenuOption("6", self.t("menu.language")), self._open_language),
			(MenuOption("0", self.t("menu.quit")), self._quit),
		]
		return menu

	def _merchant_menu(self) -> list[tuple[MenuOption, Action]]:
		return [
			(MenuOption("1", self.t("merchant.buy_potions")), self._go(View.BUY_POTIONS)),
			(MenuOption("2", self.t("merchant.sell_items")), self._go(View.SELL)),
			(MenuOption("3", self.t("merchant.equipment")), self._go(View.EQUIPMENT)),
			(MenuOption("4", self.t("merchant.stock")), self._go(View.STOCK)),
			(MenuOption("5", self.t("merchant.character")), self._go(View.CHARACTER)),
			(MenuOption("0", self.t("merchant.next_fight")), lambda: self._step(NextFight())),
			(MenuOption("q", self.t("battle.save_quit")), self._save_and_quit),
		]

	def _item_label(self, template: str, item: ItemInstance, **params: object) -> MenuOption:
		definition = self._data.item(item.item_id)
		label = self.t(
			template,
			name=definition.name,
			rarity=self.t(f"rarity.{item.rarity}"),
			slot=self.t(f"slot.{definition.slot.value}"),
			**params,
		)
		return MenuOption("", label, item.rarity)

	def _potion_shop(self) -> list[tuple[MenuOption, Action]]:
		session = self._require_session()
		menu = []
		for index, potion_id in enumerate(available_potions(session.state, self._data)):
			potion = self._data.potion(potion_id)
			label = self.t(
				"merchant.potion_option",
				name=potion.name,
				price=potion.price,
				count=session.state.player.potion_count(potion_id),
			)
			menu.append((MenuOption(list_key(index), label), self._ask_quantity(potion_id)))
		return menu

	def _sell_menu(self) -> list[tuple[MenuOption, Action]]:
		player = self._require_session().state.player
		menu = []
		for index, item in enumerate(player.bag):
			option = self._item_label("merchant.sell_option", item, gold=item_value(item, self._data))
			menu.append((MenuOption(list_key(index), option.label, option.color), self._command(SellItem(item.uid))))
		return menu

	def _equipment_menu(self) -> list[tuple[MenuOption, Action]]:
		player = self._require_session().state.player
		vocation = self._data.vocation(player.vocation_id)
		entries: list[tuple[MenuOption, Command]] = []
		for item in player.bag:
			if can_use(self._data.item(item.item_id), vocation):
				entries.append((self._item_label("merchant.equip_option", item), Equip(item.uid)))
		for slot in Slot:
			equipped = player.equipment.get(slot)
			if equipped is not None:
				entries.append((self._item_label("merchant.unequip_option", equipped), Unequip(slot)))
		return [
			(MenuOption(list_key(i), option.label, option.color), self._command(command))
			for i, (option, command) in enumerate(entries)
		]

	def _stock_menu(self) -> list[tuple[MenuOption, Action]]:
		stock = self._require_session().state.merchant_stock
		menu = []
		for index, item in enumerate(stock):
			option = self._item_label("merchant.stock_option", item, gold=stock_price(item, self._data))
			menu.append((MenuOption(list_key(index), option.label, option.color), self._command(BuyStockItem(index))))
		return menu

	def _spell_menu(self) -> list[tuple[MenuOption, Action]]:
		player = self._require_session().state.player
		levels = self._data.balance.spell_levels
		menu = []
		for index, spell_id in enumerate(self._data.vocation(player.vocation_id).spells):
			spell = self._data.spell(spell_id)
			uses = player.spell_uses.get(spell_id, 0)
			level = spell_level_for_uses(uses, levels)
			label = self.t(
				"battle.spell_option",
				name=spell.name,
				words=spell.words,
				mana=pct(spell.mana, level.mana_pct),
				level=level.level,
				uses=uses,
			)
			color = "green" if spell.kind is SpellKind.HEAL else spell.element.value
			menu.append((MenuOption(list_key(index), label, color), self._command(Cast(spell_id))))
		return menu

	def _battle_potions(self) -> list[tuple[MenuOption, Action]]:
		player = self._require_session().state.player
		owned = [p for p in self._data.potions if player.potion_count(p.id) > 0]
		return [
			(
				MenuOption(list_key(i), self.t("battle.potion_option", name=p.name, count=player.potion_count(p.id))),
				self._command(UsePotion(p.id)),
			)
			for i, p in enumerate(owned)
		]

	# ── actions ───────────────────────────────────────────────────────────────

	def _choose_language(self, locale: str) -> Action:
		def action() -> None:
			self._set_locale(locale)
			self._services.settings.save(Settings(locale))
			self.view = self._language_return

		return action

	def _open_language(self) -> None:
		self._language_return = View.TITLE
		self.view = View.LANGUAGE

	def _quit(self) -> None:
		self.exit_requested = True

	def _new_run(self) -> None:
		self.session = None
		self.view = View.DIFFICULTY

	def _choose_difficulty(self, difficulty_id: str) -> Action:
		def action() -> None:
			self._difficulty = difficulty_id
			self.input_buffer = ""
			self.view = View.NAME

		return action

	def _choose_vocation(self, vocation_id: str) -> Action:
		def action() -> None:
			seed = self._seed if self._seed is not None else self._seed_source()
			services = self._services
			session, events = GameSession.start(
				self._data,
				RunConfig(self._name, vocation_id, self._difficulty),
				seed,
				repositories=services.repositories,
				clock=services.clock,
				game_version=services.version,
			)
			self.session = session
			self.log.clear()
			self._record(StepResult(events, []))
			self.view = View.MERCHANT

		return action

	def _continue(self) -> None:
		services = self._services
		session = GameSession.resume(self._data, services.repositories, services.clock, services.version)
		if session is None:
			return
		self.session = session
		self.log.clear()
		self.log.append(self.t("menu.welcome_back", name=session.state.player.name, round=session.state.round))
		self.view = View.MERCHANT

	def _ask_quantity(self, potion_id: str) -> Action:
		def action() -> None:
			self._potion_id = potion_id
			self.input_buffer = ""
			self.view = View.QUANTITY

		return action

	def _command(self, command: Command) -> Action:
		return lambda: self._step(command)

	def _save_and_quit(self) -> None:
		if self.session is not None:
			self.session.save_and_quit()
		self.session = None
		self.view = View.TITLE

	def _require_session(self) -> GameSession:
		if self.session is None:
			raise RuntimeError(f"view {self.view} needs an active run")
		return self.session

	def _step(self, command: Command) -> None:
		session = self._require_session()
		result = session.step(command)
		self._record(result)
		phase = session.state.phase
		if phase is Phase.BATTLE:
			self.view = View.BATTLE
		elif phase is Phase.GAME_OVER:
			self.view = View.GAME_OVER
		elif self.view in BATTLE_VIEWS:
			self.view = View.MERCHANT

	def _record(self, result: StepResult) -> None:
		session = self._require_session()
		cues: list[str] = []
		for event in result.events:
			text = self._formatter.format(event, session.state)
			if event["type"] == "error":
				self.message = text
				continue
			self.log.append(text)
			if event["type"] in {"player_attacked", "spell_cast"} and event.get("damage", 0) != 0:
				cues.append("hurt")
			elif event["type"] == "monster_attacked":
				cues.append("attack")
		for achievement in result.achievements:
			name = self.t(f"achievement.{achievement.id}.name")
			self.log.append(self.t("achievement.unlocked", name=name))
		self.animation_cues = cues

	# ── informative bodies ────────────────────────────────────────────────────

	def _profile(self) -> ProfileService:
		if self.session is not None:
			return self.session.profile
		return ProfileService(self._data, self._services.repositories.profile.load())

	def _body(self) -> list[str]:
		match self.view:
			case View.CHARACTER:
				return self._character_sheet()
			case View.GAME_OVER:
				return self._game_over_summary()
			case View.HALL_OF_FAME:
				return self._hall_of_fame()
			case View.BESTIARY:
				return self._bestiary()
			case View.ACHIEVEMENTS:
				return self._achievements()
			case View.MERCHANT:
				player = self._require_session().state.player
				return [self.t("merchant.welcome", name=player.name, gold=player.gold)]
			case View.SELL if not self._require_session().state.player.bag:
				return [self.t("merchant.empty_bag")]
			case View.STOCK if not self._require_session().state.merchant_stock:
				return [self.t("merchant.empty_stock")]
			case View.POTIONS if not any(self._require_session().state.player.potions.values()):
				return [self.t("battle.no_potions")]
			case _:
				return []

	def _character_sheet(self) -> list[str]:
		player = self._require_session().state.player
		sheet = build_sheet(player, self._data)
		balance = self._data.balance
		lines = [
			self.t("character.level", level=player.level, xp=player.xp, next=xp_for_level(player.level + 1)),
			self.t(
				"character.magic_level",
				magicLevel=player.magic_level,
				spent=player.mana_spent,
				next=mana_for_magic_level(player.magic_level, balance),
			),
			self.t("character.hp_mp", hp=player.hp, maxHp=sheet.max_hp, mp=player.mp, maxMp=sheet.max_mp),
			self.t(
				"character.melee",
				min=sheet.melee_min,
				max=sheet.melee_max,
				element=self.t(f"element.{sheet.weapon_element.value}"),
			),
		]
		stats = [
			("stat.armor", sheet.armor),
			("stat.hpRegen", sheet.hp_regen),
			("stat.mpRegen", sheet.mp_regen),
			("stat.critChance", sheet.crit_chance),
			("stat.critDamage", sheet.crit_damage),
			("stat.spellPower", sheet.spell_power),
			("stat.physicalDamage", sheet.physical_damage),
			("stat.dodge", sheet.dodge),
			("stat.parry", sheet.parry),
			("stat.lifeLeech", sheet.life_leech),
			("stat.manaLeech", sheet.mana_leech),
		]
		lines += [self.t("character.stat_line", stat=self.t(key), value=value) for key, value in stats if value]
		lines += [
			self.t("character.stat_line", stat=self.t(f"element.{element.value}"), value=f"{value}%")
			for element, value in sheet.protections.items()
			if value
		]
		lines.append("")
		lines.append(self.t("character.equipment"))
		for slot in Slot:
			item = player.equipment.get(slot)
			if item is None:
				lines.append(self.t("character.empty_slot", slot=self.t(f"slot.{slot.value}")))
			else:
				lines.append(
					self.t(
						"character.slot",
						slot=self.t(f"slot.{slot.value}"),
						item=self._data.item(item.item_id).name,
						rarity=self.t(f"rarity.{item.rarity}"),
					)
				)
		lines.append(self.t("character.bag", count=len(player.bag), capacity=balance.bag_capacity))
		return lines

	def _game_over_summary(self) -> list[str]:
		state = self._require_session().state
		monster = self._data.creature(state.death_cause).name if state.death_cause else "?"
		return [
			self.t(
				"gameover.summary",
				name=state.player.name,
				vocation=self.t(f"vocation.{state.player.vocation_id}"),
				round=state.round,
				monster=monster,
			),
			self.t(
				"gameover.stats",
				level=state.player.level,
				damage=state.stats.damage_dealt,
				kills=sum(state.stats.kills.values()),
				bosses=state.stats.bosses_killed,
			),
		]

	def _hall_of_fame(self) -> list[str]:
		hall = self._profile().profile.hall_of_fame
		if not hall:
			return [self.t("hall.empty")]
		return [
			self.t(
				"hall.entry",
				position=index + 1,
				name=entry.name,
				vocation=self.t(f"vocation.{entry.vocation}"),
				difficulty=self.t(f"difficulty.{entry.difficulty}"),
				round=entry.round,
				level=entry.level,
				date=entry.ended_at[:10],
			)
			for index, entry in enumerate(hall)
		]

	def _bestiary(self) -> list[str]:
		profile = self._profile()
		lines = []
		for creature in sorted(self._data.monsters + self._data.bosses, key=lambda c: (c.tier, c.is_boss, c.name)):
			entry = profile.profile.bestiary.get(creature.id)
			if entry is None:
				lines.append(self.t("bestiary.unknown", tier=creature.tier + 1))
			elif profile.revealed(creature.id):
				weak = [self.t(f"element.{e.value}") for e in Element if creature.resistance(e) > 100]
				strong = [self.t(f"element.{e.value}") for e in Element if creature.resistance(e) < 100]
				lines.append(
					self.t(
						"bestiary.entry_revealed",
						name=creature.name,
						tier=creature.tier + 1,
						kills=entry.kills,
						weak=", ".join(weak) or "—",
						strong=", ".join(strong) or "—",
					)
				)
			else:
				lines.append(self.t("bestiary.entry", name=creature.name, tier=creature.tier + 1, kills=entry.kills))
		return lines

	def _achievements(self) -> list[str]:
		unlocked = self._profile().profile.achievements
		lines = []
		for achievement in self._data.achievements:
			name = self.t(f"achievement.{achievement.id}.name")
			description = self.t(f"achievement.{achievement.id}.description", value=achievement.value)
			unlock = unlocked.get(achievement.id)
			if unlock is None:
				lines.append(self.t("achievements.locked", name=name, description=description))
			else:
				lines.append(
					self.t("achievements.unlocked", name=name, description=description, date=unlock.unlocked_at[:10])
				)
		return lines
