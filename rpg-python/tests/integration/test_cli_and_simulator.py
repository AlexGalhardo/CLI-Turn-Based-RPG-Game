from pathlib import Path

import pytest

from rpg.__main__ import main
from rpg.application.simulator import simulate
from rpg.domain.definitions import GameData
from rpg.infrastructure.i18n import Translator
from rpg.presentation.cli import parse_args


def test_simulate_summary(data: GameData) -> None:
	summary = simulate(data, "warrior", "normal", 3, base_seed=10)
	assert summary.runs == 3
	assert 0 <= summary.wins <= 3
	assert summary.win_rate_pct == summary.wins * 100 // 3
	assert 1 <= summary.min_round <= summary.median_round <= summary.max_round
	assert sum(count for _, count in summary.top_killers) <= 3
	with pytest.raises(ValueError, match="positive"):
		simulate(data, "warrior", "normal", 0)


def test_main_simulate_prints_report(capsys: pytest.CaptureFixture[str]) -> None:
	assert main(["--simulate", "1", "--vocation", "mage", "--difficulty", "hard", "--seed", "5"]) == 0
	output = capsys.readouterr().out
	assert "median" in output
	assert "win %" in output
	assert "mage" in output


def test_simulator_counts_won_runs(data: GameData) -> None:
	summary = simulate(data, "archer", "easy", 2, base_seed=2002)
	assert summary.wins >= 1
	assert summary.max_round == data.balance.final_round


def test_main_simulate_rejects_unknown_vocation(capsys: pytest.CaptureFixture[str]) -> None:
	assert main(["--simulate", "1", "--vocation", "knight"]) == 2
	assert "invalid run config" in capsys.readouterr().err


def test_parse_args_defaults_and_validation() -> None:
	options = parse_args([])
	assert options.seed is None
	assert options.no_anim is False
	options = parse_args(["--seed", "42", "--lang", "pt-BR", "--no-anim", "--data-dir", "x"])
	assert (options.seed, options.lang, options.no_anim, options.data_dir) == (42, "pt-BR", True, "x")
	with pytest.raises(SystemExit):
		parse_args(["--seed", "-1"])
	with pytest.raises(SystemExit):
		parse_args(["--simulate", "0"])
	with pytest.raises(SystemExit):
		parse_args(["--lang", "fr"])


def test_translator(shared_dir: Path) -> None:
	english = Translator(shared_dir)
	portuguese = Translator(shared_dir, "pt-BR")
	assert english.t("event.gold_looted", amount=5) == "You looted 5 gold."
	assert portuguese.t("event.gold_looted", amount=5) == "Você saqueou 5 de ouro."
	assert english.t("missing.key") == "missing.key"
	assert english.t("event.gold_looted") == "You looted {amount} gold."
	assert portuguese.has("menu.quit")
	with pytest.raises(ValueError, match="unsupported"):
		Translator(shared_dir, "fr")
