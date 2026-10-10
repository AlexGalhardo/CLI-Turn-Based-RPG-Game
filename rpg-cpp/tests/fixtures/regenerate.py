"""Replays the Python save fixtures with the reference bot and rewrites the *_continued.json expectations.

Run after a balance change: cd rpg-python && uv run python ../rpg-cpp/tests/fixtures/regenerate.py
"""

import json
import shutil
import tempfile
from datetime import UTC, datetime, timedelta
from pathlib import Path

from rpg.application.bot import GreedyBot
from rpg.application.game_session import GameSession, Repositories
from rpg.domain.enums import Phase
from rpg.infrastructure.data_loader import load_game_data
from rpg.infrastructure.repositories import FileHistoryRepository, FileProfileRepository, FileSaveRepository

root = Path(__file__).resolve().parents[3]
fixtures = root / "rpg-cpp/tests/fixtures"
data = load_game_data(root / "shared")


class FakeClock:
	def __init__(self) -> None:
		self.current = datetime(2026, 9, 27, 12, 0, 0, tzinfo=UTC)

	def now(self) -> datetime:
		self.current += timedelta(seconds=10)
		return self.current


for save, profile, out in (
	("python_save.json", "python_profile.json", "python_save_continued.json"),
	("python_save_v1.json", "python_profile_v1.json", "python_save_v1_continued.json"),
):
	with tempfile.TemporaryDirectory() as tmp:
		d = Path(tmp)
		shutil.copy(fixtures / save, d / "save.json")
		shutil.copy(fixtures / profile, d / "profile.json")
		repos = Repositories(FileSaveRepository(d), FileHistoryRepository(d), FileProfileRepository(d))
		session = GameSession.resume(data, repos, FakeClock(), "1")
		assert session is not None
		bot = GreedyBot(data)
		steps = 0
		while session.state.phase != Phase.GAME_OVER:
			session.step(bot.choose(session.state))
			steps += 1
		state = session.state
		old = json.loads((fixtures / out).read_text(encoding="utf-8"))
		new = {
			"steps": steps,
			"round": state.round,
			"level": state.player.level,
			"gold": state.player.gold,
			"rngState": session.engine.rng_state,
			"deathCause": state.death_cause or "",
			"won": state.won,
			"stats": state.stats.to_dict(),
		}
		assert list(new) == list(old), (list(new), list(old))
		assert list(new["stats"]) == list(old["stats"])
		raw = (fixtures / out).read_text(encoding="utf-8")
		indent = None if "\n" not in raw.strip() else "\t"
		(fixtures / out).write_text(
			json.dumps(new, indent=indent, ensure_ascii=False) + ("\n" if raw.endswith("\n") else ""),
			encoding="utf-8",
			newline="\n",
		)
		print(out, {k: v for k, v in new.items() if k != "stats"})
