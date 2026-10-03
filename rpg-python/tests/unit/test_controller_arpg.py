"""M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen."""

from dataclasses import replace
from pathlib import Path

from rpg.application.profile import HallOfFameEntry
from rpg.domain.character import item_score
from rpg.domain.definitions import GameData
from rpg.domain.entities import AffixRoll, ItemInstance
from rpg.domain.enums import Phase, Slot, Stat
from rpg.infrastructure.repositories import Settings
from rpg.presentation.controller import AUTO_BATTLE_BASE_MS, Controller, View
from rpg.presentation.render import STYLE_DIM, STYLE_GAIN, STYLE_LOSS, STYLE_WARNING, format_delta
from tests.conftest import calm, with_test_items
from tests.unit.test_controller import make_controller, start_run, view_of


def test_settings_toggle_and_persist(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	controller.press("6")
	labels = [option.label for option in controller.options()]
	assert labels[1] == "Auto-equip on new runs: Off"
	assert labels[2] == "Auto-battle speed: 1x"
	controller.press("2")
	controller.press("3")
	assert controller.settings == Settings(None, auto_equip=True, battle_speed=2)
	assert controller.auto_battle_interval_ms() == AUTO_BATTLE_BASE_MS // 2
	assert controller.services.settings.load() == Settings(None, auto_equip=True, battle_speed=2)
	controller.press("3")
	assert controller.settings.battle_speed == 1
	controller.press("1")
	controller.press("1")
	assert view_of(controller) is View.SETTINGS
	assert controller.services.settings.load() == Settings("en", auto_equip=True, battle_speed=1)


def test_new_run_auto_equip_step_marks_the_default(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = make_controller(data, shared_dir, tmp_path)
	controller.services.settings.save(Settings("en", auto_equip=True))
	controller = make_controller(data, shared_dir, tmp_path)
	start_run(controller, auto_equip_key="0")
	assert view_of(controller) is View.VOCATION
	controller.press("1")
	assert view_of(controller) is View.AUTO_EQUIP
	assert controller.title() == controller.t("new_run.auto_equip")
	labels = [option.label for option in controller.options()]
	assert labels[0].endswith("(default)")
	assert not labels[1].endswith("(default)")
	controller.press("1")
	assert view_of(controller) is View.MERCHANT
	assert controller.session is not None
	assert controller.session.state.config.auto_equip is True


def _battle_controller(data: GameData, shared_dir: Path, tmp_path: Path, vocation_key: str = "1") -> Controller:
	controller = make_controller(data, shared_dir, tmp_path)
	start_run(controller, vocation_key=vocation_key)
	controller.press("0")
	assert view_of(controller) is View.BATTLE
	return controller


def test_auto_battle_menu_and_instant_run(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _battle_controller(data, shared_dir, tmp_path)
	controller.press("5")
	assert view_of(controller) is View.AUTO_BATTLE
	assert [option.key for option in controller.options()] == ["1", "2", "3", "0"]
	controller.press("0")
	assert view_of(controller) is View.BATTLE
	controller.press("5")
	controller.press("3")
	assert controller.auto_battle_active
	assert controller.log[-1] == controller.t("auto_battle.started", mode=controller.t("auto_battle.balanced"))
	session = controller.session
	assert session is not None
	turn = session.state.turn
	controller.press("1")
	assert session.state.turn == turn
	controller.run_auto_battle()
	assert not controller.auto_battle_active
	assert session.state.phase is not Phase.BATTLE
	assert view_of(controller) in {View.MERCHANT, View.GAME_OVER}
	assert controller.auto_battle_step() is False


def test_auto_battle_steps_one_turn_at_a_time(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _battle_controller(data, shared_dir, tmp_path)
	session = controller.session
	assert session is not None
	monster = session.state.monster
	assert monster is not None
	monster.hp = monster.max_hp = 1_000_000
	controller.press("5")
	controller.press("1")
	turn = session.state.turn
	session.state.player.hp = 1_000_000
	assert controller.auto_battle_step() is True
	assert session.state.turn == turn + 1


def test_victory_screen_end_run(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _victory_controller(data, shared_dir, tmp_path)
	assert controller.title() == controller.t("victory.title")
	assert "Ferumbras" in controller.body_lines()[0]
	controller.press("9")
	assert view_of(controller) is View.VICTORY
	controller.press("1")
	assert view_of(controller) is View.GAME_OVER
	assert controller.title() == controller.t("gameover.title_won")
	assert "won the run" in controller.body_lines()[0]
	controller.press("2")
	controller.press("3")
	assert "WON" in controller.body_lines()[0]


def test_victory_screen_continue(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _victory_controller(data, shared_dir, tmp_path)
	controller.press("2")
	assert view_of(controller) is View.MERCHANT
	assert controller.session is not None
	assert controller.session.state.won is True


def test_continue_saved_victory_returns_to_the_victory_screen(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _victory_controller(data, shared_dir, tmp_path)
	controller.session = None
	controller.view = View.TITLE
	controller.press("1")
	assert view_of(controller) is View.VICTORY


def _victory_controller(data: GameData, shared_dir: Path, tmp_path: Path) -> Controller:
	controller = make_controller(calm(data), shared_dir, tmp_path)
	start_run(controller)
	session = controller.session
	assert session is not None
	session.state.round = data.balance.final_round - 1
	controller.press("0")
	for _ in range(50):
		monster = session.state.monster
		if monster is None:
			break
		monster.hp = 1
		controller.press("1")
	assert view_of(controller) is View.VICTORY
	return controller


def _equipment_controller(data: GameData, shared_dir: Path, tmp_path: Path) -> Controller:
	controller = make_controller(with_test_items(data), shared_dir, tmp_path)
	start_run(controller)
	controller.press("3")
	assert view_of(controller) is View.EQUIPMENT
	return controller


def test_equipment_screen_lists_every_slot_and_the_bag(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _equipment_controller(data, shared_dir, tmp_path)
	game_data = controller.services.data
	session = controller.session
	assert session is not None
	player = session.state.player
	player.bag.extend(
		[
			ItemInstance(900, "test_axe", "rare", 0),
			ItemInstance(901, "test_rod", "common", 0),
			ItemInstance(902, "test_helmet", "legendary", 9),
		]
	)
	lines = controller.body_lines()
	colors = controller.body_colors()
	starter = player.equipment[Slot.WEAPON]
	assert lines[0] == f"EQUIPPED · total score {item_score(starter, game_data)}"
	assert lines[1].startswith("Weapon: Sword [Common] · Lv 1")
	assert lines[2] == "Shield: - empty -"
	assert colors[2] == STYLE_WARNING
	assert len([line for line in lines if "- empty -" in line]) == 7
	assert lines[-1] == "BAG (usable)"
	options = controller.options()
	assert [option.key for option in options] == ["1", "2", "3", "0"]
	axe, helmet, slot = options[0], options[1], options[2]
	delta = item_score(ItemInstance(900, "test_axe", "rare", 0), game_data) - item_score(starter, game_data)
	assert axe.detail == format_delta(delta)
	assert axe.detail_color == STYLE_GAIN
	assert axe.color == "rare"
	assert helmet.color == STYLE_DIM
	assert helmet.label.endswith("requires Lv 37")
	assert slot.label == "Weapon: Sword [Common]"


def test_comparison_shows_stat_and_score_deltas(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _equipment_controller(data, shared_dir, tmp_path)
	session = controller.session
	assert session is not None
	player = session.state.player
	player.equipment[Slot.HELMET] = ItemInstance(800, "test_helmet", "common", 0, (AffixRoll(Stat.DODGE, 3),))
	player.bag.append(ItemInstance(801, "test_helmet", "rare", 0, (AffixRoll(Stat.CRIT_CHANCE, 2),)))
	controller.press("1")
	assert view_of(controller) is View.COMPARE
	assert controller.title() == "Helmet: Test Helmet → Test Helmet"
	lines = controller.body_lines()
	colors = dict(zip(lines, controller.body_colors(), strict=True))
	assert colors["Armor: 10 → 15 (+5)"] == STYLE_GAIN
	assert colors["Max HP: 50 → 75 (+25)"] == STYLE_GAIN
	assert colors["Critical chance: 0 → 2 (+2)"] == STYLE_GAIN
	assert colors["Dodge: 3 → 0 (-3)"] == STYLE_LOSS
	assert colors["Affixes gained: +2 Critical chance"] == STYLE_GAIN
	assert colors["Affixes lost: +3 Dodge"] == STYLE_LOSS
	assert any(line.startswith("Score: ") for line in lines)
	assert [option.key for option in controller.options()] == ["1", "0"]
	controller.press("1")
	assert view_of(controller) is View.EQUIPMENT
	assert player.equipment[Slot.HELMET].uid == 801


def test_comparison_warns_about_the_required_level(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _equipment_controller(data, shared_dir, tmp_path)
	session = controller.session
	assert session is not None
	session.state.player.bag.append(ItemInstance(810, "test_axe", "common", 3))
	controller.press("1")
	assert controller.body_colors()[-1] == STYLE_LOSS
	assert controller.body_lines()[-1] == "Requires level 13 (you are level 1)."
	controller.press("1")
	assert controller.message == controller.t("error.level_too_low")
	assert view_of(controller) is View.EQUIPMENT
	controller.press("0")
	controller.press("3")
	controller.press(list_key_of(controller, "Weapon"))
	assert view_of(controller) is View.EQUIPPED_SLOT
	assert controller.title() == "Weapon"
	assert controller.body_lines()[1] == "Attack: 6"
	controller.press("1")
	assert view_of(controller) is View.EQUIPMENT
	assert Slot.WEAPON not in session.state.player.equipment
	controller.press("0")


def list_key_of(controller: Controller, prefix: str) -> str:
	return next(option.key for option in controller.options() if option.label.startswith(prefix))


def test_compare_and_slot_views_survive_missing_items(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	controller = _equipment_controller(data, shared_dir, tmp_path)
	session = controller.session
	assert session is not None
	assert controller.body_lines()[-1] == controller.t("equipment.bag_empty")
	controller.view = View.COMPARE
	assert controller.body_lines() == []
	assert controller.title() == controller.t("merchant.equipment")
	controller.view = View.EQUIPPED_SLOT
	controller.press("0")
	controller.view = View.EQUIPPED_SLOT
	session.state.player.equipment.clear()
	assert controller.body_lines() == [controller.t("equipment.empty")]


def test_monster_view_and_hall_of_fame_markers(data: GameData, shared_dir: Path, tmp_path: Path) -> None:
	elite_data = replace(data, balance=replace(data.balance, elite_chance_pct=100))
	controller = make_controller(elite_data, shared_dir, tmp_path)
	repository = controller.services.repositories.profile
	profile = repository.load()
	profile.hall_of_fame.append(HallOfFameEntry("r", "Ana", "mage", "hard", 100, 50, "2026-01-01T00:00:00Z", True))
	repository.save(profile)
	controller.press("3")
	assert "WON" in controller.body_lines()[0]
	controller.press("0")
	start_run(controller)
	controller.press("0")
	monster = controller.monster_view()
	assert monster is not None
	assert monster.enemy_class == "elite"
	assert any("ELITE" in line for line in controller.log)
