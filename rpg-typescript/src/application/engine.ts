/** The game engine: a pure state machine `step(command) -> events` (docs/architecture.md). */
import { buildSheet, itemValue } from "../domain/character";
import { type EnemyClassDef, type GameData, UnknownIdError } from "../domain/definitions";
import { type MonsterInstance, Player } from "../domain/entities";
import { roundInfo } from "../domain/formulas";
import { Rng } from "../domain/rng";
import { autoEquip } from "./auto-equip";
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
			case "end_run":
			case "continue_run":
				if (phase !== "victory") return [error(ErrorCode.INVALID_PHASE)];
				return command.type === "end_run" ? this.endRun() : this.continueRun();
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
				enemyClass: monster.enemyClass,
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
		const events: Event[] = [
			event("monster_killed", {
				monsterId: monster.creatureId,
				isBoss: monster.isBoss,
				enemyClass: monster.enemyClass,
			}),
		];
		events.push(...new Progression(this.data).gainExperience(player, monster.xp));

		const gold = this.rng.roll(monster.goldMin, monster.goldMax);
		player.gold += gold;
		events.push(event("gold_looted", { amount: gold }));
		events.push(...this.drops(monster));
		if (state.config.autoEquip) events.push(...autoEquip(state, this.data));

		player.statuses = [];
		player.stunCooldown = 0;
		player.defending = false;
		const sheet = buildSheet(player, this.data);
		player.hp = Math.min(player.hp, sheet.maxHp);
		player.mp = Math.min(player.mp, sheet.maxMp);
		state.monster = null;
		state.turn = 0;
		if (state.round === this.data.balance.finalRound) {
			state.won = true;
			state.phase = "victory";
			events.push(event("run_won", { round: state.round }));
			return events;
		}
		state.phase = "merchant";
		events.push(...new Merchant(this.data, this.rng, state).enter());
		return events;
	}

	/** One rule for the three classes: chance(100) and chance(0) consume nothing (docs/game-design.md §8). */
	private drops(monster: MonsterInstance): Event[] {
		const row = this.data.balance.enemyClass(monster.enemyClass);
		const events: Event[] = [];
		if (this.rng.chance(row.dropChancePct)) {
			for (let i = 0; i < row.drops; i++) events.push(...this.dropItem(row));
		}
		if (this.rng.chance(row.potionDropPct)) events.push(...this.dropPotion());
		return events;
	}

	private dropItem(row: EnemyClassDef): Event[] {
		const state = this.state;
		const balance = this.data.balance;
		const item = generateItem(this.data, this.rng, {
			vocation: this.data.vocation(state.player.vocationId),
			tier: roundInfo(state.round, balance, this.data.tierCount).tier,
			weights: row.rarityWeights,
			uid: state.nextItemUid,
		});
		if (item === null) return [];
		state.takeItemUid();
		const events: Event[] = [event("item_dropped", { uid: item.uid, itemId: item.itemId, rarity: item.rarity })];
		if (state.player.bag.length >= balance.bagCapacity) {
			const value = itemValue(item, this.data);
			state.player.gold += value;
			events.push(event("item_auto_sold", { uid: item.uid, itemId: item.itemId, gold: value }));
		} else {
			state.player.bag.push(item);
		}
		return events;
	}

	private dropPotion(): Event[] {
		const state = this.state;
		const unlocked = this.data.potions.filter((potion) => potion.unlockRound <= state.round);
		if (unlocked.length === 0) return [];
		const potion = this.rng.pick(unlocked);
		state.player.potions.set(potion.id, state.player.potionCount(potion.id) + 1);
		return [event("potion_dropped", { potionId: potion.id })];
	}

	private endRun(): Event[] {
		const state = this.state;
		state.phase = "game_over";
		state.deathCause = null;
		return [event("run_ended", { won: state.won })];
	}

	private continueRun(): Event[] {
		const state = this.state;
		state.phase = "merchant";
		return new Merchant(this.data, this.rng, state).enter();
	}

	private defeat(): Event[] {
		const state = this.state;
		const monsterId = state.monster === null ? "" : state.monster.creatureId;
		state.phase = "game_over";
		state.deathCause = monsterId;
		return [event("player_died", { monsterId, round: state.round })];
	}
}
