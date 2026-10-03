"""Framework-independent UI state machine: which screen is shown, its options and what each key does.

The Textual app only renders this controller. The other five ports implement the same controller, which is what
keeps the six interfaces practically identical (docs/tui.md).
"""

import secrets
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass, replace
from enum import StrEnum
from pathlib import Path

from rpg.application.auto_battle import AutoBattleMode, AutoBattlePolicy
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
from rpg.application.game_session import GameSession, Repositories, StepResult
from rpg.application.loot import can_use
from rpg.application.merchant import available_potions, stock_price
from rpg.application.ports import Clock
from rpg.application.profile import ProfileService
from rpg.application.run_state import RunConfig
from rpg.domain.character import (
	build_sheet,
	equipment_score,
	item_score,
	item_stats,
	item_value,
	required_level,
)
from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance
from rpg.domain.enums import EQUIPMENT_SLOT_ORDER, Element, Phase, Slot, SpellKind, Stat
from rpg.domain.formulas import mana_for_magic_level, pct, round_info, spell_level_for_uses, xp_for_level
from rpg.infrastructure.i18n import DEFAULT_LOCALE, SUPPORTED_LOCALES, Translator
from rpg.infrastructure.repositories import BATTLE_SPEEDS, Settings, SettingsRepository
from rpg.presentation.event_text import EventFormatter
from rpg.presentation.render import (
	STYLE_DIM,
	STYLE_GAIN,
	STYLE_LOSS,
	STYLE_WARNING,
	delta_style,
	format_delta,
	list_key,
)

MAX_LOG_LINES = 50
MAX_NAME_LENGTH = 16
MAX_QUANTITY_DIGITS = 2
PAGE_SIZE = 10
# Auto-battle pace (docs/tui.md): one turn every 600 ms at 1x, 300 ms at 2x; instant with --no-anim.
AUTO_BATTLE_BASE_MS = 600
# Safety net: a fight that somehow never ends hands control back to the player.
MAX_AUTO_BATTLE_TURNS = 10_000


class View(StrEnum):
	LANGUAGE = "language"
	TITLE = "title"
	SETTINGS = "settings"
	DIFFICULTY = "difficulty"
	NAME = "name"
	VOCATION = "vocation"
	AUTO_EQUIP = "auto_equip"
	MERCHANT = "merchant"
	BUY_POTIONS = "buy_potions"
	QUANTITY = "quantity"
	SELL = "sell"
	EQUIPMENT = "equipment"
	COMPARE = "compare"
	EQUIPPED_SLOT = "equipped_slot"
	STOCK = "stock"
	CHARACTER = "character"
	BATTLE = "battle"
	SPELLS = "spells"
	POTIONS = "potions"
	AUTO_BATTLE = "auto_battle"
	VICTORY = "victory"
	GAME_OVER = "game_over"
	HALL_OF_FAME = "hall_of_fame"
	BESTIARY = "bestiary"
	ACHIEVEMENTS = "achievements"


BATTLE_VIEWS = {View.BATTLE, View.SPELLS, View.POTIONS, View.AUTO_BATTLE}
TEXT_INPUT_VIEWS = {View.NAME, View.QUANTITY}
PAGED_VIEWS = {View.HALL_OF_FAME, View.BESTIARY, View.ACHIEVEMENTS, View.CHARACTER}
STYLED_VIEWS = {View.EQUIPMENT, View.COMPARE, View.EQUIPPED_SLOT}


@dataclass(frozen=True, slots=True)
class MenuOption:
	key: str
	label: str
	color: str | None = None
	detail: str = ""
	detail_color: str | None = None


@dataclass(frozen=True, slots=True)
class BodyLine:
	text: str
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
	enemy_class: str
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
		self.settings = settings
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
		self._vocation = ""
		self._potion_id = ""
		self._compare_uid = 0
		self._slot = Slot.WEAPON
		self._auto_battle: AutoBattlePolicy | None = None
		self._auto_mode = AutoBattleMode.MELEE
		self._auto_turns = 0

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
			case View.SETTINGS:
				return self.t("settings.title")
			case View.DIFFICULTY:
				return self.t("new_run.difficulty")
			case View.NAME:
				return self.t("new_run.name")
			case View.VOCATION:
				return self.t("new_run.vocation")
			case View.AUTO_EQUIP:
				return self.t("new_run.auto_equip")
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
			case View.COMPARE:
				return self._compare_title()
			case View.EQUIPPED_SLOT:
				return self.t("slot." + self._slot.value)
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
			case View.AUTO_BATTLE:
				return self.t("auto_battle.title")
			case View.VICTORY:
				return self.t("victory.title")
			case View.GAME_OVER:
				won = self.session is not None and self.session.state.won
				return self.t("gameover.title_won" if won else "gameover.title")
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
		lines = [line.text for line in self._styled_body()] if self.view in STYLED_VIEWS else self._body()
		if self.view not in PAGED_VIEWS or len(lines) <= PAGE_SIZE:
			return lines
		pages = (len(lines) + PAGE_SIZE - 1) // PAGE_SIZE
		self.page = min(self.page, pages - 1)
		start = self.page * PAGE_SIZE
		return [*lines[start : start + PAGE_SIZE], "", self.t("menu.page", page=self.page + 1, pages=pages)]

	def body_colors(self) -> list[str | None]:
		"""Colour of each line of `body_lines()` (semantic styles from render.py or rarity ids)."""
		if self.view in STYLED_VIEWS:
			return [line.color for line in self._styled_body()]
		return [None] * len(self.body_lines())

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
			enemy_class=monster.enemy_class,
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
		if self.auto_battle_active:
			return
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

	# ── auto-battle (docs/game-design.md §13, docs/tui.md) ──────────────────────

	@property
	def auto_battle_active(self) -> bool:
		return self._auto_battle is not None

	def auto_battle_interval_ms(self) -> int:
		return AUTO_BATTLE_BASE_MS // self.settings.battle_speed

	def auto_battle_step(self) -> bool:
		"""Plays one auto-battle turn. Returns True while the fight goes on (the renderer's timer keeps ticking)."""
		policy = self._auto_battle
		session = self.session
		if policy is None or session is None or session.state.phase is not Phase.BATTLE:
			self._auto_battle = None
			return False
		self._auto_turns += 1
		self._step(policy.choose(session.state))
		if session.state.phase is not Phase.BATTLE or self._auto_turns >= MAX_AUTO_BATTLE_TURNS:
			self._auto_battle = None
		return self.auto_battle_active

	def run_auto_battle(self) -> None:
		"""Instant mode (--no-anim): plays the whole fight at once."""
		while self.auto_battle_step():
			pass

	def _start_auto_battle(self, mode: AutoBattleMode) -> Action:
		def action() -> None:
			self._auto_mode = mode
			self._auto_turns = 0
			self._auto_battle = AutoBattlePolicy(self._data, mode)
			self.view = View.BATTLE
			self.log.append(self.t("auto_battle.started", mode=self.t(f"auto_battle.{mode.value}")))

		return action

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
			case View.SETTINGS:
				return self._settings_menu()
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
			case View.AUTO_EQUIP:
				return self._auto_equip_menu()
			case View.MERCHANT:
				return self._merchant_menu()
			case View.BUY_POTIONS:
				return [*self._potion_shop(), self._back(View.MERCHANT)]
			case View.SELL:
				return [*self._sell_menu(), self._back(View.MERCHANT)]
			case View.EQUIPMENT:
				return [*self._equipment_menu(), self._back(View.MERCHANT)]
			case View.COMPARE:
				return [
					(MenuOption("1", self.t("equipment.equip")), self._equip_compared),
					self._back(View.EQUIPMENT),
				]
			case View.EQUIPPED_SLOT:
				return [
					(MenuOption("1", self.t("equipment.unequip")), self._unequip_slot),
					self._back(View.EQUIPMENT),
				]
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
					(MenuOption("5", self.t("battle.auto")), self._go(View.AUTO_BATTLE)),
					(MenuOption("q", self.t("battle.save_quit")), self._save_and_quit),
				]
			case View.AUTO_BATTLE:
				modes = [
					(MenuOption(str(i + 1), self.t(f"auto_battle.{mode.value}")), self._start_auto_battle(mode))
					for i, mode in enumerate(AutoBattleMode)
				]
				return [*modes, self._back(View.BATTLE)]
			case View.VICTORY:
				return [
					(MenuOption("1", self.t("victory.end_run")), lambda: self._step(EndRun())),
					(MenuOption("2", self.t("victory.continue")), lambda: self._step(ContinueRun())),
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
			(MenuOption("6", self.t("menu.settings")), self._go(View.SETTINGS)),
			(MenuOption("0", self.t("menu.quit")), self._quit),
		]
		return menu

	def _on_off(self, enabled: bool) -> str:
		return self.t("settings.on" if enabled else "settings.off")

	def _settings_menu(self) -> list[tuple[MenuOption, Action]]:
		settings = self.settings
		return [
			(
				MenuOption("1", self.t("settings.language", language=self.t(f"language.{self.locale}"))),
				self._open_language,
			),
			(
				MenuOption("2", self.t("settings.auto_equip", state=self._on_off(settings.auto_equip))),
				lambda: self._save_settings(replace(self.settings, auto_equip=not self.settings.auto_equip)),
			),
			(
				MenuOption("3", self.t("settings.battle_speed", speed=settings.battle_speed)),
				self._cycle_battle_speed,
			),
			self._back(View.TITLE),
		]

	def _save_settings(self, settings: Settings) -> None:
		self.settings = settings
		self._services.settings.save(settings)

	def _cycle_battle_speed(self) -> None:
		index = BATTLE_SPEEDS.index(self.settings.battle_speed) if self.settings.battle_speed in BATTLE_SPEEDS else 0
		self._save_settings(replace(self.settings, battle_speed=BATTLE_SPEEDS[(index + 1) % len(BATTLE_SPEEDS)]))

	def _auto_equip_menu(self) -> list[tuple[MenuOption, Action]]:
		default = self.t("new_run.default")
		menu = []
		for key, enabled in (("1", True), ("2", False)):
			label = self.t("new_run.auto_equip_on" if enabled else "new_run.auto_equip_off")
			if enabled == self.settings.auto_equip:
				label = f"{label} {default}"
			menu.append((MenuOption(key, label), self._start_run(enabled)))
		return [*menu, self._back(View.VOCATION)]

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

	def _usable_bag(self) -> list[ItemInstance]:
		player = self._require_session().state.player
		vocation = self._data.vocation(player.vocation_id)
		return [item for item in player.bag if can_use(self._data.item(item.item_id), vocation)]

	def _score_delta(self, item: ItemInstance) -> int:
		"""Score of `item` minus the score of what is equipped in its slot (0 for an empty slot)."""
		equipped = self._require_session().state.player.equipment.get(self._data.item(item.item_id).slot)
		return item_score(item, self._data) - (0 if equipped is None else item_score(equipped, self._data))

	def _equipment_menu(self) -> list[tuple[MenuOption, Action]]:
		"""Usable bag items first (keys 1..n, as in docs/tui.md), then the equipped slots."""
		player = self._require_session().state.player
		entries: list[tuple[MenuOption, Action]] = []
		for item in self._usable_bag():
			definition = self._data.item(item.item_id)
			level = required_level(item, self._data)
			label = self.t(
				"equipment.bag_option",
				name=definition.name,
				rarity=self.t(f"rarity.{item.rarity}"),
				slot=self.t(f"slot.{definition.slot.value}"),
				level=level,
				score=item_score(item, self._data),
			)
			too_high = level > player.level
			if too_high:
				label = f"{label} · {self.t('equipment.requires_level', level=level)}"
			delta = self._score_delta(item)
			option = MenuOption(
				"", label, STYLE_DIM if too_high else item.rarity, format_delta(delta), delta_style(delta)
			)
			entries.append((option, self._open_compare(item.uid)))
		for slot in EQUIPMENT_SLOT_ORDER:
			equipped = player.equipment.get(slot)
			if equipped is not None:
				label = self.t(
					"equipment.slot_option",
					slot=self.t(f"slot.{slot.value}"),
					name=self._data.item(equipped.item_id).name,
					rarity=self.t(f"rarity.{equipped.rarity}"),
				)
				entries.append((MenuOption("", label, equipped.rarity), self._open_slot(slot)))
		return [(replace(option, key=list_key(index)), action) for index, (option, action) in enumerate(entries)]

	def _open_compare(self, uid: int) -> Action:
		def action() -> None:
			self._compare_uid = uid
			self.view = View.COMPARE

		return action

	def _open_slot(self, slot: Slot) -> Action:
		def action() -> None:
			self._slot = slot
			self.view = View.EQUIPPED_SLOT

		return action

	def _compared_item(self) -> ItemInstance | None:
		bag = self._require_session().state.player.bag
		return next((item for item in bag if item.uid == self._compare_uid), None)

	def _equip_compared(self) -> None:
		self._step(Equip(self._compare_uid))
		self.view = View.EQUIPMENT

	def _unequip_slot(self) -> None:
		self._step(Unequip(self._slot))
		self.view = View.EQUIPMENT

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
			self._save_settings(replace(self.settings, locale=locale))
			self.view = self._language_return

		return action

	def _open_language(self) -> None:
		self._language_return = View.SETTINGS
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
			self._vocation = vocation_id
			self.view = View.AUTO_EQUIP

		return action

	def _start_run(self, auto_equip: bool) -> Action:
		def action() -> None:
			seed = self._seed if self._seed is not None else self._seed_source()
			services = self._services
			session, events = GameSession.start(
				self._data,
				RunConfig(self._name, self._vocation, self._difficulty, auto_equip),
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
		self.view = View.VICTORY if session.state.phase is Phase.VICTORY else View.MERCHANT

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
		elif phase is Phase.VICTORY:
			self.view = View.VICTORY
		elif self.view in BATTLE_VIEWS or self.view is View.VICTORY:
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
			case View.VICTORY:
				return self._victory_summary()
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

	def _run_stats_line(self) -> str:
		state = self._require_session().state
		return self.t(
			"gameover.stats",
			level=state.player.level,
			damage=state.stats.damage_dealt,
			kills=sum(state.stats.kills.values()),
			elites=state.stats.elites_killed,
			bosses=state.stats.bosses_killed,
		)

	def _game_over_summary(self) -> list[str]:
		state = self._require_session().state
		params = {
			"name": state.player.name,
			"vocation": self.t(f"vocation.{state.player.vocation_id}"),
			"round": state.round,
		}
		if state.death_cause:
			summary = self.t("gameover.summary", monster=self._data.creature(state.death_cause).name, **params)
		elif state.won:
			summary = self.t("gameover.won_summary", **params)
		else:
			summary = self.t("gameover.summary", monster="?", **params)
		return [summary, self._run_stats_line()]

	def _victory_summary(self) -> list[str]:
		state = self._require_session().state
		boss = self._data.boss_of_tier(round_info(state.round, self._data.balance, self._data.tier_count).tier)
		return [
			self.t(
				"victory.summary",
				name=state.player.name,
				vocation=self.t(f"vocation.{state.player.vocation_id}"),
				monster=boss.name,
				round=state.round,
			),
			self._run_stats_line(),
			"",
			self.t("victory.choice"),
		]

	# ── equipment screens (docs/tui.md "Equipment screen") ────────────────────

	def _styled_body(self) -> list[BodyLine]:
		match self.view:
			case View.EQUIPMENT:
				return self._equipment_body()
			case View.COMPARE:
				return self._compare_body()
			case _:
				return self._slot_body()

	def _equipment_body(self) -> list[BodyLine]:
		player = self._require_session().state.player
		lines = [BodyLine(self.t("equipment.equipped_header", score=equipment_score(player, self._data)))]
		for slot in EQUIPMENT_SLOT_ORDER:
			slot_name = self.t(f"slot.{slot.value}")
			item = player.equipment.get(slot)
			if item is None:
				lines.append(BodyLine(self.t("equipment.slot_empty", slot=slot_name), STYLE_WARNING))
				continue
			text = self.t(
				"equipment.slot_line",
				slot=slot_name,
				name=self._data.item(item.item_id).name,
				rarity=self.t(f"rarity.{item.rarity}"),
				level=required_level(item, self._data),
				score=item_score(item, self._data),
			)
			lines.append(BodyLine(text, item.rarity))
		lines.append(BodyLine(""))
		lines.append(BodyLine(self.t("equipment.bag_header")))
		if not self._usable_bag():
			lines.append(BodyLine(self.t("equipment.bag_empty")))
		return lines

	def _compare_title(self) -> str:
		item = self._compared_item()
		if item is None:
			return self.t("merchant.equipment")
		player = self._require_session().state.player
		slot = self._data.item(item.item_id).slot
		current = player.equipment.get(slot)
		return self.t(
			"equipment.compare_title",
			slot=self.t(f"slot.{slot.value}"),
			current=self.t("equipment.empty") if current is None else self._data.item(current.item_id).name,
			new=self._data.item(item.item_id).name,
		)

	def _affix_list(self, item: ItemInstance | None) -> str:
		if item is None:
			return ""
		return ", ".join(
			self.t("equipment.affix", value=affix.value, stat=self.t(f"stat.{affix.stat.value}"))
			for affix in item.affixes
		)

	def _compare_body(self) -> list[BodyLine]:
		item = self._compared_item()
		if item is None:
			return []
		player = self._require_session().state.player
		current = player.equipment.get(self._data.item(item.item_id).slot)
		new_stats = item_stats(item, self._data)
		old_stats = {} if current is None else item_stats(current, self._data)
		lines = []
		for stat in Stat:
			if stat not in new_stats and stat not in old_stats:
				continue
			old, new = old_stats.get(stat, 0), new_stats.get(stat, 0)
			text = self.t(
				"equipment.stat_delta",
				stat=self.t(f"stat.{stat.value}"),
				current=old,
				new=new,
				delta=format_delta(new - old),
			)
			lines.append(BodyLine(text, delta_style(new - old)))
		gained, lost = self._affix_list(item), self._affix_list(current)
		if gained:
			lines.append(BodyLine(self.t("equipment.affixes_gained", affixes=gained), STYLE_GAIN))
		if lost:
			lines.append(BodyLine(self.t("equipment.affixes_lost", affixes=lost), STYLE_LOSS))
		old_score = 0 if current is None else item_score(current, self._data)
		new_score = item_score(item, self._data)
		score = self.t(
			"equipment.score_delta", current=old_score, new=new_score, delta=format_delta(new_score - old_score)
		)
		lines.append(BodyLine(score, delta_style(new_score - old_score)))
		level = required_level(item, self._data)
		if level > player.level:
			lines.append(BodyLine(self.t("equipment.level_needed", level=level, current=player.level), STYLE_LOSS))
		return lines

	def _slot_body(self) -> list[BodyLine]:
		item = self._require_session().state.player.equipment.get(self._slot)
		if item is None:
			return [BodyLine(self.t("equipment.empty"), STYLE_WARNING)]
		lines = [
			BodyLine(
				self.t(
					"equipment.item_title",
					name=self._data.item(item.item_id).name,
					rarity=self.t(f"rarity.{item.rarity}"),
					level=required_level(item, self._data),
					score=item_score(item, self._data),
				),
				item.rarity,
			)
		]
		stats = item_stats(item, self._data)
		lines += [
			BodyLine(self.t("character.stat_line", stat=self.t(f"stat.{stat.value}"), value=stats[stat]))
			for stat in Stat
			if stat in stats
		]
		return lines

	def _hall_of_fame(self) -> list[str]:
		hall = self._profile().profile.hall_of_fame
		if not hall:
			return [self.t("hall.empty")]
		return [
			self.t(
				"hall.entry_won" if entry.won else "hall.entry",
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
