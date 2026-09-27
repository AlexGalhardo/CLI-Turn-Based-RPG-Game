"""End-to-end tests: the real Textual app driven by key presses (no animation, fixed seed, temp data dir)."""

import html
import json
import re
from pathlib import Path

from textual.pilot import Pilot

from rpg import __version__
from rpg.application.bot import GreedyBot
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
	UsePotion,
)
from rpg.application.game_session import Repositories
from rpg.domain.definitions import GameData
from rpg.domain.enums import Phase
from rpg.infrastructure.art import ArtLibrary
from rpg.infrastructure.repositories import (
	FileHistoryRepository,
	FileProfileRepository,
	FileSaveRepository,
	SettingsRepository,
	SystemClock,
)
from rpg.presentation.controller import Controller, Services, View
from rpg.presentation.render import list_key
from rpg.presentation.tui.app import RpgApp

SIZE = (100, 30)


def services(data: GameData, shared_dir: Path, data_dir: Path) -> Services:
	return Services(
		data=data,
		shared_dir=shared_dir,
		settings=SettingsRepository(data_dir),
		repositories=Repositories(
			FileSaveRepository(data_dir), FileHistoryRepository(data_dir), FileProfileRepository(data_dir)
		),
		clock=SystemClock(),
		version=__version__,
	)


def make_app(data: GameData, shared_dir: Path, data_dir: Path, lang: str | None = "en") -> RpgApp:
	controller = Controller(services(data, shared_dir, data_dir), seed=42, locale_override=lang)
	return RpgApp(controller, ArtLibrary(shared_dir), animate=False)


def screen_text(app: RpgApp) -> str:
	"""Plain text of the rendered screen, extracted from Textual's SVG screenshot."""
	rows: dict[str, list[str]] = {}
	for y, fragment in re.findall(r'<text[^>]* y="([\d.]+)"[^>]*>(.*?)</text>', app.export_screenshot()):
		rows.setdefault(y, []).append(fragment)
	text = "\n".join("".join(fragments) for fragments in rows.values())
	return html.unescape(text).replace("\xa0", " ")


async def type_text(pilot: Pilot[int], text: str) -> None:
	press = pilot.press
	for character in text:
		await press(character)


async def test_first_launch_language_then_new_run_flow(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	app = make_app(data, shared_dir, tmp_path, lang=None)
	async with app.run_test(size=SIZE) as pilot:
		assert current_view(app) is View.LANGUAGE
		await pilot.press("2")
		assert current_view(app) is View.TITLE
		assert "Nova jornada" in screen_text(app)
		await pilot.press("2")
		assert current_view(app) is View.DIFFICULTY
		await pilot.press("3")
		await type_text(pilot, "Ana")
		await pilot.press("enter")
		assert current_view(app) is View.VOCATION
		await pilot.press("3")
		assert current_view(app) is View.MERCHANT
		text = screen_text(app)
		assert "Ana" in text
		assert "Mago" in text
	settings = json.loads((tmp_path / "settings.json").read_text(encoding="utf-8"))
	assert settings["locale"] == "pt-BR"
	assert (tmp_path / "save.json").exists()


async def test_battle_merchant_save_quit_and_continue(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	app = make_app(data, shared_dir, tmp_path)
	async with app.run_test(size=SIZE) as pilot:
		await pilot.press("2", "2")
		await type_text(pilot, "Bo")
		await pilot.press("enter", "1")
		assert current_view(app) is View.MERCHANT
		await pilot.press("1")
		assert current_view(app) is View.BUY_POTIONS
		await pilot.press("1")
		assert current_view(app) is View.QUANTITY
		await pilot.press("1", "enter")
		assert app.controller.session is not None
		assert app.controller.session.state.player.potion_count("health_potion") == 6
		await pilot.press("0", "5")
		assert "Equipment" in screen_text(app)
		await pilot.press("0", "0")
		assert current_view(app) is View.BATTLE
		assert "HP" in screen_text(app)
		await pilot.press("1")
		await pilot.press("2")
		assert current_view(app) is View.SPELLS
		await pilot.press(list_key(0))
		await pilot.press("3")
		assert current_view(app) is View.POTIONS
		await pilot.press("escape")
		assert current_view(app) is View.BATTLE
		await pilot.press("q")
		assert current_view(app) is View.TITLE
		assert "Continue" in screen_text(app)
		await pilot.press("1")
		assert current_view(app) is View.MERCHANT
		assert app.controller.session is not None
		assert app.controller.session.info.sessions == 2


async def test_full_run_until_game_over(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	"""Plays a whole run through the UI keys, choosing what the bot would do, until death."""
	app = make_app(data, shared_dir, tmp_path)
	bot = GreedyBot(data)
	async with app.run_test(size=SIZE) as pilot:
		await pilot.press("2", "3")
		await type_text(pilot, "Hero")
		await pilot.press("enter", "1")
		controller = app.controller
		for _ in range(5000):
			session = controller.session
			assert session is not None
			if session.state.phase is Phase.GAME_OVER:
				break
			for key in keys_for(bot.choose(session.state), controller):
				await pilot.press(key)
		assert current_view(app) is View.GAME_OVER
		assert "GAME OVER" in screen_text(app)
		await pilot.press("2")
		await pilot.press("3")
		assert "Hero" in screen_text(app)
		await pilot.press("0", "5")
		assert "[x] First Blood" in screen_text(app)
		await pilot.press("0", "4")
		assert current_view(app) is View.BESTIARY
		await pilot.press("n", "p", "0", "0")
		assert app.return_value == 0
	assert not (tmp_path / "save.json").exists()
	assert len(list((tmp_path / "history").glob("*.json"))) == 1


def keys_for(command: Command, controller: Controller) -> list[str]:
	session = controller.session
	assert session is not None
	state = session.state
	match command:
		case Attack():
			return ["1"]
		case Defend():
			return ["4"]
		case Cast(spell_id):
			return ["2", list_key(data_spells(controller).index(spell_id))]
		case UsePotion(potion_id):
			owned = [p.id for p in controller.services.data.potions if state.player.potion_count(p.id) > 0]
			return ["3", list_key(owned.index(potion_id))]
		case NextFight():
			return ["0"]
		case BuyPotion(potion_id, quantity):
			from rpg.application.merchant import available_potions  # noqa: PLC0415

			index = available_potions(state, controller.services.data).index(potion_id)
			return ["1", list_key(index), *str(quantity), "enter", "0"]
		case SellItem(uid):
			index = [item.uid for item in state.player.bag].index(uid)
			return ["2", list_key(index), "0"]
		case Equip(uid):
			options = controller_options_after(controller, "3")
			label_index = next(i for i, o in enumerate(options) if o == uid)
			return ["3", list_key(label_index), "0"]
		case BuyStockItem(index):
			return ["4", list_key(index), "0"]
		case _:
			raise AssertionError(f"unexpected command {command}")


def data_spells(controller: Controller) -> list[str]:
	session = controller.session
	assert session is not None
	return list(controller.services.data.vocation(session.state.player.vocation_id).spells)


def controller_options_after(controller: Controller, _: str) -> list[int]:
	"""Uids in the order the equipment menu lists them (usable bag items first)."""
	from rpg.application.loot import can_use  # noqa: PLC0415

	session = controller.session
	assert session is not None
	data = controller.services.data
	vocation = data.vocation(session.state.player.vocation_id)
	return [item.uid for item in session.state.player.bag if can_use(data.item(item.item_id), vocation)]


def current_view(app: RpgApp) -> View:
	"""Reads the view through a function so type checkers don't narrow it between key presses."""
	return app.controller.view
