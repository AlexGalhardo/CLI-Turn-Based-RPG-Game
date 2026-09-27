from pathlib import Path

import pytest

from rpg import __version__
from rpg.application.game_session import Repositories
from rpg.application.profile import BestiaryEntry
from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance
from rpg.infrastructure.art import ArtLibrary, frame_for, parse_art
from rpg.infrastructure.repositories import (
	FileHistoryRepository,
	FileProfileRepository,
	FileSaveRepository,
	SettingsRepository,
	SystemClock,
)
from rpg.presentation.controller import PAGE_SIZE, Controller, Services, View
from rpg.presentation.render import bar, hp_color, list_index, list_key


def make_controller(data: GameData, shared_dir: Path, tmp_path: Path, lang: str | None = "en") -> Controller:
	services = Services(
		data=data,
		shared_dir=shared_dir,
		settings=SettingsRepository(tmp_path),
		repositories=Repositories(
			FileSaveRepository(tmp_path), FileHistoryRepository(tmp_path), FileProfileRepository(tmp_path)
		),
		clock=SystemClock(),
		version=__version__,
	)
	return Controller(services, seed=7, locale_override=lang)


def start_run(controller: Controller, name: str = "Zed", vocation_key: str = "1") -> None:
	controller.press("2")
	controller.press("2")
	for character in name:
		controller.press(character)
	controller.press("enter")
	controller.press(vocation_key)


def test_name_validation_and_editing(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	controller.press("2")
	controller.press("1")
	assert view_of(controller) is View.NAME
	controller.press("enter")
	assert controller.message == controller.t("new_run.name_invalid")
	for character in "Abcdefghijklmnopqrstuvwxyz":
		controller.press(character)
	assert controller.input_buffer == "Abcdefghijklmnop"
	controller.press("backspace")
	assert controller.input_prompt() == "> Abcdefghijklmno_"
	controller.press("escape")
	assert view_of(controller) is View.DIFFICULTY
	controller.press("0")
	assert view_of(controller) is View.TITLE


def test_title_without_save_has_no_continue(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	keys = [option.key for option in controller.options()]
	assert "1" not in keys
	controller.press("1")
	assert view_of(controller) is View.TITLE
	controller.press("0")
	assert controller.exit_requested


def test_language_switch_from_title(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	controller.press("6")
	assert view_of(controller) is View.LANGUAGE
	controller.press("2")
	assert view_of(controller) is View.TITLE
	assert controller.locale == "pt-BR"
	assert controller.title() == "CLI Turn-Based RPG"
	assert any(option.label == "Sair" for option in controller.options())


def test_merchant_menus(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	start_run(controller)
	session = controller.session
	assert session is not None
	assert controller.title() == controller.t("merchant.title_start")
	player = session.state.player
	controller.press("2")
	assert controller.body_lines() == [controller.t("merchant.empty_bag")]
	controller.press("0")
	player.bag.append(ItemInstance(900, "hand_axe", "rare", 0))
	player.bag.append(ItemInstance(901, "bow", "common", 0))
	controller.press("3")
	labels = [option.label for option in controller.options()]
	assert any("Hand Axe" in label for label in labels)
	assert not any("Bow" in label for label in labels)
	controller.press("1")
	assert player.equipment[next(iter(player.equipment))].uid in {900, 1}
	controller.press("0")
	controller.press("2")
	sell_keys = [option.key for option in controller.options()]
	assert sell_keys[-1] == "0"
	controller.press(sell_keys[0])
	controller.press("0")
	controller.press("4")
	assert len(controller.options()) == len(session.state.merchant_stock) + 1
	player.gold = 0
	controller.press("1")
	assert controller.message == controller.t("error.not_enough_gold")
	controller.press("0")
	controller.press("1")
	controller.press("1")
	controller.press("enter")
	assert view_of(controller) is View.BUY_POTIONS
	controller.press("0")
	controller.press("5")
	assert view_of(controller) is View.CHARACTER
	assert any("Equipment" in line for line in controller.body_lines())


def test_battle_submenus_and_messages(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	start_run(controller, vocation_key="3")
	controller.press("0")
	assert view_of(controller) is View.BATTLE
	assert controller.monster_view() is not None
	assert controller.player_view() is not None
	session = controller.session
	assert session is not None
	session.state.player.potions.clear()
	controller.press("3")
	assert controller.body_lines() == [controller.t("battle.no_potions")]
	controller.press("0")
	session.state.player.mp = 0
	controller.press("2")
	controller.press(list_key(0))
	assert controller.message == controller.t("error.not_enough_mana")
	controller.press("escape")
	controller.press("4")
	assert controller.animation_cues in (["attack"], [])
	controller.press("q")
	assert view_of(controller) is View.TITLE
	assert controller.session is None


def test_bestiary_paging_and_reveal(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	repository = controller.services.repositories.profile
	profile = repository.load()
	profile.bestiary["rat"] = BestiaryEntry(9, "2026-01-01T00:00:00Z")
	profile.bestiary["bat"] = BestiaryEntry(1, "2026-01-01T00:00:00Z")
	repository.save(profile)
	controller.press("4")
	lines = controller.body_lines()
	assert len(lines) == PAGE_SIZE + 2
	assert any(line.startswith("Rat") and "weak" in line for line in lines)
	assert any(line.startswith("Bat") and "weak" not in line for line in lines)
	controller.press("n")
	assert controller.body_lines() != lines
	for _ in range(50):
		controller.press("n")
	assert controller.body_lines()[-1].startswith("Page 12/12")
	controller.press("p")
	controller.press("0")
	controller.press("3")
	assert controller.body_lines() == [controller.t("hall.empty")]
	controller.press("0")
	controller.press("5")
	assert all(line.startswith("[ ]") for line in controller.body_lines()[:PAGE_SIZE])


def test_render_helpers() -> None:
	assert bar(0, 100, 10) == "░" * 10
	assert bar(1, 100, 10) == "█" + "░" * 9
	assert bar(100, 100, 10) == "█" * 10
	assert bar(5, 0, 4) == "░" * 4
	assert hp_color(60, 100) == "green"
	assert hp_color(30, 100) == "yellow"
	assert hp_color(10, 100) == "red"
	assert list_key(0) == "1"
	assert list_key(9) == "a"
	assert list_index("a") == 9
	assert list_index("!") is None


def test_art_parsing(shared_dir: Path, data: GameData) -> None:
	animations = parse_art("@idle\n a\n%%\n b\n@hurt\n x\n")
	assert animations == {"idle": ((" a",), (" b",)), "hurt": ((" x",),)}
	assert frame_for(animations, "idle", 3) == (" b",)
	assert frame_for(animations, "attack", 0) == (" a",)
	assert frame_for({}, "idle", 0) == ()
	with pytest.raises(ValueError, match="before"):
		parse_art("oops")
	with pytest.raises(ValueError, match="separator"):
		parse_art("%%")
	library = ArtLibrary(shared_dir)
	for creature in data.monsters + data.bosses:
		assert frame_for(library.for_creature(creature), "idle", 0)


def view_of(controller: Controller) -> View:
	"""Reads the view through a function so type checkers don't narrow it between key presses."""
	return controller.view
