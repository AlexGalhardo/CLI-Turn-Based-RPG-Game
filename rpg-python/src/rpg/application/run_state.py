from dataclasses import dataclass, field

from rpg.application.statistics import RunStatistics
from rpg.domain.entities import ItemInstance, MonsterInstance, Player
from rpg.domain.enums import Phase
from rpg.domain.json_types import JsonObject, JsonValue, json_int, json_list, json_obj, json_str


@dataclass(frozen=True, slots=True)
class RunConfig:
	name: str
	vocation_id: str
	difficulty_id: str

	def to_dict(self) -> JsonObject:
		return {"name": self.name, "vocation": self.vocation_id, "difficulty": self.difficulty_id}

	@staticmethod
	def from_dict(raw: JsonValue) -> RunConfig:
		data = json_obj(raw)
		return RunConfig(json_str(data["name"]), json_str(data["vocation"]), json_str(data["difficulty"]))


@dataclass(slots=True)
class RunState:
	"""Everything needed to continue a run, except the PRNG state (kept by the engine)."""

	seed: int
	config: RunConfig
	player: Player
	phase: Phase = Phase.MERCHANT
	round: int = 0
	turn: int = 0
	monster: MonsterInstance | None = None
	merchant_stock: list[ItemInstance] = field(default_factory=list)
	next_item_uid: int = 1
	death_cause: str | None = None
	stats: RunStatistics = field(default_factory=RunStatistics)

	def take_item_uid(self) -> int:
		uid = self.next_item_uid
		self.next_item_uid += 1
		return uid

	def to_dict(self) -> JsonObject:
		return {
			"seed": self.seed,
			"config": self.config.to_dict(),
			"player": self.player.to_dict(),
			"phase": self.phase.value,
			"round": self.round,
			"turn": self.turn,
			"monster": None if self.monster is None else self.monster.to_dict(),
			"merchantStock": [item.to_dict() for item in self.merchant_stock],
			"nextItemUid": self.next_item_uid,
			"deathCause": self.death_cause,
			"stats": self.stats.to_dict(),
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> RunState:
		data = json_obj(raw)
		monster_raw = data["monster"]
		death_cause = data["deathCause"]
		return RunState(
			seed=json_int(data["seed"]),
			config=RunConfig.from_dict(data["config"]),
			player=Player.from_dict(data["player"]),
			phase=Phase(json_str(data["phase"])),
			round=json_int(data["round"]),
			turn=json_int(data["turn"]),
			monster=None if monster_raw is None else MonsterInstance.from_dict(monster_raw),
			merchant_stock=[ItemInstance.from_dict(i) for i in json_list(data["merchantStock"])],
			next_item_uid=json_int(data["nextItemUid"]),
			death_cause=None if death_cause is None else json_str(death_cause),
			stats=RunStatistics.from_dict(data["stats"]),
		)
