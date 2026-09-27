/** The game engine: a pure state machine `step(command) -> events` (docs/architecture.md). */
import { buildSheet, itemValue } from "../domain/character";
import { type GameData, UnknownIdError } from "../domain/definitions";
import { Player } from "../domain/entities";
import { roundInfo } from "../domain/formulas";
import { Rng } from "../domain/rng";
import { Battle } from "./battle";
import type { BattleCommand, Command } from "./commands";
import { ErrorCode, type Event, error, event } from "./events";
import { generateItem } from "./loot";
import { Merchant } from "./merchant";
import { Progression } from "./progression";
import { type RunConfig, RunState } from "./run-state";
import { spawnMonster } from "./spawner";

export class InvalidRunConfigError extends Error {}

export class GameEngine {
	constructor(
		readonly data: GameData,
		readonly state: RunState,
		private readonly rng: Rng,
	) {}

	get rngState(): number {
		return this.rng.state;
	}

	static newRun(data: GameData, config: RunConfig, seed: number): [GameEngine, Event[]] {
		let vocationId: string;
		try {
			vocationId = data.vocation(config.vocationId).id;
			data.balance.difficulty(config.difficultyId);
		} catch (exc) {
			if (exc instanceof UnknownIdError) throw new InvalidRunConfigError(`invalid run config: ${exc.message}`);
			throw exc;
		}
		const vocation = data.vocation(vocationId);
		const player = new Player(
			config.name,
			vocation.id,
			vocation.startHp,
			vocation.startMp,
			data.balance.startingGold,
		);
		player.potions = new Map(data.balance.startingPotions);
		const state = new RunState(seed, config, player);
		const starter = data.item(vocation.starterWeapon);
		player.equipment.set(starter.slot, {
			uid: state.takeItemUid(),
			itemId: starter.id,
			rarity: "common",
			tier: starter.tier,
			affixes: [],
		});
		const engine = new GameEngine(data, state, new Rng(seed));
		const events: Event[] = [
			event("run_started", { seed, vocation: vocation.id, difficulty: config.difficultyId }),
			...new Merchant(data, engine.rng, state).enter(),
		];
		return [engine, events];
	}

	static restore(data: GameData, state: RunState, rngState: number): GameEngine {
		return new GameEngine(data, state, new Rng(rngState));
	}

	step(command: Command): Event[] {
		const events = this.dispatch(command);
		this.state.stats.record(events, this.state.round);
		return events;
	}

	private dispatch(command: Command): Event[] {
		const phase = this.state.phase;
		switch (command.type) {
			case "attack":
			case "cast":
			case "potion":
			case "defend":
				if (phase !== "battle") return [error(ErrorCode.INVALID_PHASE)];
				return this.battleTurn(command);
			case "next_fight":
				if (phase !== "merchant") return [error(ErrorCode.INVALID_PHASE)];
				return this.nextFight();
			default:
				if (phase !== "merchant") return [error(ErrorCode.INVALID_PHASE)];
				return new Merchant(this.data, this.rng, this.state).handle(command);
		}
	}

	private nextFight(): Event[] {
		const state = this.state;
		state.round += 1;
		const difficulty = this.data.balance.difficulty(state.config.difficultyId);
		const [monster, info] = spawnMonster(this.data, this.rng, state.round, difficulty);
		state.monster = monster;
		state.phase = "battle";
		state.turn = 1;
		state.merchantStock = [];
		return [
			event("round_started", {
				round: state.round,
				tier: info.tier,
				cycle: info.cycle,
				monsterId: monster.creatureId,
				isBoss: monster.isBoss,
				hp: monster.hp,
			}),
		];
	}

	private battleTurn(command: BattleCommand): Event[] {
		const battle = new Battle(this.data, this.rng, this.state);
		const invalid = battle.validate(command);
		if (invalid !== null) return [invalid];
		const [events, outcome] = battle.playTurn(command);
		if (outcome === "victory") events.push(...this.victory());
		else if (outcome === "defeat") events.push(...this.defeat());
		return events;
	}

	private victory(): Event[] {
		const state = this.state;
		const player = state.player;
		const monster = state.monster;
		if (monster === null) throw new Error("victory without a monster");
		const events: Event[] = [event("monster_killed", { monsterId: monster.creatureId, isBoss: monster.isBoss })];
		events.push(...new Progression(this.data).gainExperience(player, monster.xp));

		const gold = this.rng.roll(monster.goldMin, monster.goldMax);
		player.gold += gold;
		events.push(event("gold_looted", { amount: gold }));
		events.push(...this.drops(monster.isBoss));

		player.statuses = [];
		player.stunCooldown = 0;
		player.defending = false;
		const sheet = buildSheet(player, this.data);
		player.hp = Math.min(player.hp, sheet.maxHp);
		player.mp = Math.min(player.mp, sheet.maxMp);
		state.monster = null;
		state.phase = "merchant";
		state.turn = 0;
		events.push(...new Merchant(this.data, this.rng, state).enter());
		return events;
	}

	private drops(isBoss: boolean): Event[] {
		const state = this.state;
		const balance = this.data.balance;
		let count: number;
		let table: string;
		if (isBoss) {
			count = balance.bossDrops;
			table = "boss";
		} else if (this.rng.chance(balance.dropChancePct)) {
			count = 1;
			table = "monster";
		} else {
			return [];
		}

		const tier = roundInfo(state.round, balance, this.data.tierCount).tier;
		const difficulty = balance.difficulty(state.config.difficultyId);
		const vocation = this.data.vocation(state.player.vocationId);
		const events: Event[] = [];
		for (let i = 0; i < count; i++) {
			const item = generateItem(this.data, this.rng, {
				vocation,
				tier,
				table,
				difficulty,
				uid: state.nextItemUid,
			});
			if (item === null) continue;
			state.takeItemUid();
			events.push(event("item_dropped", { uid: item.uid, itemId: item.itemId, rarity: item.rarity }));
			if (state.player.bag.length >= balance.bagCapacity) {
				const value = itemValue(item, this.data);
				state.player.gold += value;
				events.push(event("item_auto_sold", { uid: item.uid, itemId: item.itemId, gold: value }));
			} else {
				state.player.bag.push(item);
			}
		}
		return events;
	}

	private defeat(): Event[] {
		const state = this.state;
		const monsterId = state.monster === null ? "" : state.monster.creatureId;
		state.phase = "game_over";
		state.deathCause = monsterId;
		return [event("player_died", { monsterId, round: state.round })];
	}
}
