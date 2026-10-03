"""Cross-run profile: bestiary, achievements and Hall of Fame (docs/game-design.md §11)."""

from dataclasses import dataclass, field

from rpg.application.events import Event
from rpg.application.run_state import RunState
from rpg.domain.definitions import AchievementDef, GameData
from rpg.domain.json_types import JsonObject, JsonValue, json_bool, json_int, json_list, json_obj, json_str

PROFILE_SCHEMA_VERSION = 2
HALL_OF_FAME_SIZE = 10
BESTIARY_REVEAL_KILLS = 5


@dataclass(slots=True)
class BestiaryEntry:
	kills: int
	first_killed_at: str


@dataclass(frozen=True, slots=True)
class Unlock:
	unlocked_at: str
	run_id: str


@dataclass(frozen=True, slots=True)
class HallOfFameEntry:
	run_id: str
	name: str
	vocation: str
	difficulty: str
	round: int
	level: int
	ended_at: str
	won: bool = False

	def to_dict(self) -> JsonObject:
		return {
			"runId": self.run_id,
			"name": self.name,
			"vocation": self.vocation,
			"difficulty": self.difficulty,
			"round": self.round,
			"level": self.level,
			"endedAt": self.ended_at,
			"won": self.won,
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> HallOfFameEntry:
		data = json_obj(raw)
		return HallOfFameEntry(
			run_id=json_str(data["runId"]),
			name=json_str(data["name"]),
			vocation=json_str(data["vocation"]),
			difficulty=json_str(data["difficulty"]),
			round=json_int(data["round"]),
			level=json_int(data["level"]),
			ended_at=json_str(data["endedAt"]),
			won=json_bool(data["won"]),
		)


@dataclass(slots=True)
class Profile:
	bestiary: dict[str, BestiaryEntry] = field(default_factory=dict)
	achievements: dict[str, Unlock] = field(default_factory=dict)
	hall_of_fame: list[HallOfFameEntry] = field(default_factory=list)

	def to_dict(self) -> JsonObject:
		return {
			"schemaVersion": PROFILE_SCHEMA_VERSION,
			"bestiary": {
				key: {"kills": entry.kills, "firstKilledAt": entry.first_killed_at}
				for key, entry in sorted(self.bestiary.items())
			},
			"achievements": {
				key: {"unlockedAt": unlock.unlocked_at, "runId": unlock.run_id}
				for key, unlock in sorted(self.achievements.items())
			},
			"hallOfFame": [entry.to_dict() for entry in self.hall_of_fame],
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> Profile:
		data = json_obj(raw)
		bestiary = {
			key: BestiaryEntry(json_int(json_obj(v)["kills"]), json_str(json_obj(v)["firstKilledAt"]))
			for key, v in json_obj(data["bestiary"]).items()
		}
		achievements = {
			key: Unlock(json_str(json_obj(v)["unlockedAt"]), json_str(json_obj(v)["runId"]))
			for key, v in json_obj(data["achievements"]).items()
		}
		hall = [HallOfFameEntry.from_dict(entry) for entry in json_list(data["hallOfFame"])]
		return Profile(bestiary=bestiary, achievements=achievements, hall_of_fame=hall)


def _hall_of_fame_key(entry: HallOfFameEntry) -> tuple[bool, int, int, str]:
	return (not entry.won, -entry.round, -entry.level, entry.ended_at)


class ProfileService:
	"""Feeds the profile from engine events. Returns the achievements unlocked by each step."""

	def __init__(self, data: GameData, profile: Profile) -> None:
		self._data = data
		self.profile = profile

	def observe(self, events: list[Event], state: RunState, now: str, run_id: str) -> list[AchievementDef]:
		for event in events:
			if event["type"] == "monster_killed":
				monster_id = str(event["monsterId"])
				entry = self.profile.bestiary.get(monster_id)
				if entry is None:
					self.profile.bestiary[monster_id] = BestiaryEntry(1, now)
				else:
					entry.kills += 1
		unlocked = []
		for achievement in self._data.achievements:
			if achievement.id in self.profile.achievements:
				continue
			if self._progress(achievement, state) >= achievement.value:
				self.profile.achievements[achievement.id] = Unlock(now, run_id)
				unlocked.append(achievement)
		return unlocked

	def _progress(self, achievement: AchievementDef, state: RunState) -> int:
		player = state.player
		match achievement.type:
			case "kills_total":
				return sum(entry.kills for entry in self.profile.bestiary.values())
			case "bosses_total":
				boss_ids = {boss.id for boss in self._data.bosses}
				return sum(e.kills for key, e in self.profile.bestiary.items() if key in boss_ids)
			case "round_reached":
				return state.round
			case "level_reached":
				return player.level
			case "legendary_found":
				return state.stats.items_dropped.get("legendary", 0)
			case "spell_level_3":
				threshold = self._data.balance.spell_levels[-1].uses
				return sum(1 for uses in player.spell_uses.values() if uses >= threshold)
			case "gold_held":
				return player.gold
			case "distinct_monsters":
				return len(self.profile.bestiary)
			case "hard_round_reached":
				return state.round if state.config.difficulty_id == "hard" else 0
			case "run_won":
				return 1 if state.won else 0
			case _:
				return 0

	def record_finished_run(self, entry: HallOfFameEntry) -> None:
		ranking = sorted([*self.profile.hall_of_fame, entry], key=_hall_of_fame_key)
		self.profile.hall_of_fame = ranking[:HALL_OF_FAME_SIZE]

	def revealed(self, monster_id: str) -> bool:
		entry = self.profile.bestiary.get(monster_id)
		return entry is not None and entry.kills >= BESTIARY_REVEAL_KILLS
