"""Headless balance simulator: the bot plays many runs and we aggregate how often it wins and how far it gets."""

from collections import Counter
from dataclasses import dataclass

from rpg.application.bot import GreedyBot
from rpg.application.engine import GameEngine
from rpg.application.run_state import RunConfig
from rpg.domain.definitions import GameData
from rpg.domain.enums import Phase

MAX_STEPS_PER_RUN = 200_000


@dataclass(frozen=True, slots=True)
class RunResult:
	round: int
	level: int
	death_cause: str
	won: bool


@dataclass(frozen=True, slots=True)
class SimulationSummary:
	vocation: str
	difficulty: str
	runs: int
	wins: int
	min_round: int
	p10_round: int
	median_round: int
	p90_round: int
	max_round: int
	mean_level: int
	top_killers: tuple[tuple[str, int], ...]

	@property
	def win_rate_pct(self) -> int:
		return self.wins * 100 // self.runs


def play_one(data: GameData, config: RunConfig, seed: int) -> RunResult:
	engine, _ = GameEngine.new_run(data, config, seed)
	bot = GreedyBot(data)
	for _ in range(MAX_STEPS_PER_RUN):
		if engine.state.phase is Phase.GAME_OVER:
			break
		engine.step(bot.choose(engine.state))
	else:
		raise RuntimeError(f"run did not finish (seed {seed})")
	state = engine.state
	return RunResult(round=state.round, level=state.player.level, death_cause=state.death_cause or "", won=state.won)


def _percentile(sorted_values: list[int], percent: int) -> int:
	return sorted_values[min(len(sorted_values) - 1, len(sorted_values) * percent // 100)]


def simulate(data: GameData, vocation: str, difficulty: str, runs: int, base_seed: int = 1) -> SimulationSummary:
	if runs <= 0:
		raise ValueError("runs must be positive")
	results = [play_one(data, RunConfig("Bot", vocation, difficulty), base_seed + i) for i in range(runs)]
	rounds = sorted(r.round for r in results)
	killers = Counter(r.death_cause for r in results if r.death_cause)
	return SimulationSummary(
		vocation=vocation,
		difficulty=difficulty,
		runs=runs,
		wins=sum(1 for r in results if r.won),
		min_round=rounds[0],
		p10_round=_percentile(rounds, 10),
		median_round=_percentile(rounds, 50),
		p90_round=_percentile(rounds, 90),
		max_round=rounds[-1],
		mean_level=sum(r.level for r in results) // runs,
		top_killers=tuple(sorted(killers.items(), key=lambda kv: (-kv[1], kv[0]))[:3]),
	)
