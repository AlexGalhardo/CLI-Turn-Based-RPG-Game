import { type ItemInstance, itemFromJson, itemToJson, MonsterInstance, Player } from "../domain/entities";
import { PHASES, type Phase, parseEnum } from "../domain/enums";
import { field, type JsonObject, type JsonValue, jsonInt, jsonList, jsonObj, jsonStr } from "../domain/json-types";
import { RunStatistics } from "./statistics";

export class RunConfig {
	constructor(
		readonly name: string,
		readonly vocationId: string,
		readonly difficultyId: string,
	) {}

	toJson(): JsonObject {
		return { name: this.name, vocation: this.vocationId, difficulty: this.difficultyId };
	}

	static fromJson(raw: JsonValue): RunConfig {
		const data = jsonObj(raw);
		return new RunConfig(
			jsonStr(field(data, "name")),
			jsonStr(field(data, "vocation")),
			jsonStr(field(data, "difficulty")),
		);
	}
}

/** Everything needed to continue a run, except the PRNG state (kept by the engine). */
export class RunState {
	phase: Phase = "merchant";
	round = 0;
	turn = 0;
	monster: MonsterInstance | null = null;
	merchantStock: ItemInstance[] = [];
	nextItemUid = 1;
	deathCause: string | null = null;
	stats = new RunStatistics();

	constructor(
		readonly seed: number,
		readonly config: RunConfig,
		public player: Player,
	) {}

	takeItemUid(): number {
		const uid = this.nextItemUid;
		this.nextItemUid += 1;
		return uid;
	}

	toJson(): JsonObject {
		return {
			seed: this.seed,
			config: this.config.toJson(),
			player: this.player.toJson(),
			phase: this.phase,
			round: this.round,
			turn: this.turn,
			monster: this.monster === null ? null : this.monster.toJson(),
			merchantStock: this.merchantStock.map(itemToJson),
			nextItemUid: this.nextItemUid,
			deathCause: this.deathCause,
			stats: this.stats.toJson(),
		};
	}

	static fromJson(raw: JsonValue): RunState {
		const data = jsonObj(raw);
		const state = new RunState(
			jsonInt(field(data, "seed")),
			RunConfig.fromJson(field(data, "config")),
			Player.fromJson(field(data, "player")),
		);
		state.phase = parseEnum(PHASES, jsonStr(field(data, "phase")), "phase");
		state.round = jsonInt(field(data, "round"));
		state.turn = jsonInt(field(data, "turn"));
		const monster = field(data, "monster");
		state.monster = monster === null ? null : MonsterInstance.fromJson(monster);
		state.merchantStock = jsonList(field(data, "merchantStock")).map(itemFromJson);
		state.nextItemUid = jsonInt(field(data, "nextItemUid"));
		const deathCause = field(data, "deathCause");
		state.deathCause = deathCause === null ? null : jsonStr(deathCause);
		state.stats = RunStatistics.fromJson(field(data, "stats"));
		return state;
	}

	/** Deep copy through the save format (used for merchant snapshots). */
	clone(): RunState {
		return RunState.fromJson(JSON.parse(JSON.stringify(this.toJson())) as JsonValue);
	}
}
