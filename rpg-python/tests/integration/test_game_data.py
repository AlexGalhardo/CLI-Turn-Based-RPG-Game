import json
import shutil
from pathlib import Path

import pytest

from rpg.domain.definitions import GameData, UnknownIdError
from rpg.domain.enums import Stat, StatusKind
from rpg.infrastructure.data_loader import DataError, load_game_data
from rpg.infrastructure.paths import find_shared_dir, resolve_data_dir


def test_content_requirements(data: GameData) -> None:
	assert len(data.monsters) >= 100
	assert data.tier_count == 10
	assert {v.id for v in data.vocations} == {"warrior", "archer", "mage"}
	for tier in range(data.tier_count):
		assert len(data.monsters_in_tier(tier)) >= 9
		assert data.boss_of_tier(tier).is_boss


def test_cross_references_are_valid(data: GameData) -> None:
	for vocation in data.vocations:
		data.item(vocation.starter_weapon)
		for spell_id in vocation.spells:
			data.spell(spell_id)
	for creature in data.monsters + data.bosses:
		assert creature.family in data.families
		for attack in creature.attacks:
			assert attack.min <= attack.max
			if attack.status is not None:
				data.status(attack.status.status)
		if creature.is_boss:
			assert creature.charge_attack is not None
			creature.attack(creature.charge_attack)
	for spell in data.spells:
		if spell.level3_bonus.status is not None:
			assert data.status(spell.level3_bonus.status).kind in set(StatusKind)
	for potion_id, _ in data.balance.starting_potions:
		data.potion(potion_id)
	ids = [c.id for c in data.monsters + data.bosses]
	assert len(ids) == len(set(ids))


def test_balance_m8_tables(data: GameData) -> None:
	balance = data.balance
	assert [r.id for r in balance.rarities] == ["common", "rare", "legendary", "mythic"]
	assert [r.stat_pct for r in balance.rarities] == [100, 150, 200, 300]
	assert [level.effect_pct for level in balance.spell_levels] == [100, 150, 200]
	assert set(balance.rarity_weights) == {"merchant"}
	for enemy_class in balance.enemy_classes:
		assert set(enemy_class.rarity_weights) <= {r.id for r in balance.rarities}
	assert balance.enemy_class("elite").stat_pct > balance.enemy_class("normal").stat_pct
	assert set(balance.item_score_weights) == set(Stat)
	assert data.boss_of_tier(data.tier_count - 1).id == "ferumbras"
	assert balance.final_round == balance.rounds_per_tier * data.tier_count


def test_lookup_errors(data: GameData) -> None:
	with pytest.raises(UnknownIdError):
		data.spell("avada_kedavra")
	with pytest.raises(UnknownIdError):
		data.balance.difficulty("nightmare")
	with pytest.raises(UnknownIdError):
		data.balance.rarity("epic")
	with pytest.raises(UnknownIdError):
		data.balance.enemy_class("champion")
	with pytest.raises(UnknownIdError):
		data.balance.auto_battle.mode("berserk")
	with pytest.raises(UnknownIdError):
		data.creature("rat").attack("laser")


def test_invalid_data_raises_data_error(shared_dir: Path, tmp_path: Path) -> None:
	copy = tmp_path / "shared"
	shutil.copytree(shared_dir / "data", copy / "data")
	vocations = copy / "data" / "vocations.json"
	document = json.loads(vocations.read_text(encoding="utf-8"))
	del document["vocations"][0]["startHp"]
	vocations.write_text(json.dumps(document), encoding="utf-8")
	with pytest.raises(DataError, match="startHp"):
		load_game_data(copy)
	vocations.write_text("{ not json", encoding="utf-8")
	with pytest.raises(DataError, match=r"vocations\.json"):
		load_game_data(copy)


def test_paths(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
	monkeypatch.setenv("RPG_SHARED_DIR", str(tmp_path))
	assert find_shared_dir() == tmp_path
	monkeypatch.delenv("RPG_SHARED_DIR")
	with pytest.raises(FileNotFoundError):
		find_shared_dir(tmp_path)
	assert resolve_data_dir("custom") == Path("custom")
	monkeypatch.setenv("RPG_DATA_DIR", str(tmp_path / "x"))
	assert resolve_data_dir() == tmp_path / "x"
	monkeypatch.delenv("RPG_DATA_DIR")
	assert resolve_data_dir().name == ".cli-turn-based-rpg"
