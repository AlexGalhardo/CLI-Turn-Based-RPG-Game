/**
 * Use case that wraps the pure engine with time, persistence and the profile.
 * The engine stays deterministic; everything that depends on the clock or the filesystem happens here.
 */
import type { AchievementDef, GameData } from "../domain/definitions";
import type { Command } from "./commands";
import { GameEngine } from "./engine";
import type { Event } from "./events";
import type { Clock, HistoryRepository, ProfileRepository, SaveRepository } from "./ports";
import { ProfileService } from "./profile";
import type { RunConfig, RunState } from "./run-state";
import { formatTimestamp, IMPLEMENTATION, makeRunId, type RunRecord, SaveGame, SessionInfo } from "./save-game";

export interface StepResult {
	readonly events: Event[];
	readonly achievements: AchievementDef[];
}

export interface Repositories {
	readonly saves: SaveRepository;
	readonly history: HistoryRepository;
	readonly profile: ProfileRepository;
}

export interface SessionContext {
	readonly repositories: Repositories;
	readonly clock: Clock;
	readonly gameVersion: string;
}

export class GameSession {
	readonly profile: ProfileService;
	finishedRecord: RunRecord | null = null;
	#segmentStarted: Date;
	#merchantSnapshot: [RunState, number] | null = null;

	constructor(
		readonly data: GameData,
		readonly engine: GameEngine,
		readonly info: SessionInfo,
		private readonly context: SessionContext,
	) {
		this.profile = new ProfileService(data, context.repositories.profile.load());
		this.#segmentStarted = context.clock.now();
	}

	get state(): RunState {
		return this.engine.state;
	}

	// ── creation ──────────────────────────────────────────────────────────────

	static start(data: GameData, config: RunConfig, seed: number, context: SessionContext): [GameSession, Event[]] {
		const [engine, events] = GameEngine.newRun(data, config, seed);
		const now = context.clock.now();
		const info = new SessionInfo(makeRunId(now, seed), formatTimestamp(now));
		const session = new GameSession(data, engine, info, context);
		session.afterStep(events);
		return [session, events];
	}

	static resume(data: GameData, context: SessionContext): GameSession | null {
		const save = context.repositories.saves.load();
		if (save === null) return null;
		const engine = GameEngine.restore(data, save.run, save.rngState);
		save.session.sessions += 1;
		const session = new GameSession(data, engine, save.session, context);
		session.#merchantSnapshot = [save.run.clone(), save.rngState];
		return session;
	}

	// ── play ──────────────────────────────────────────────────────────────────

	step(command: Command): StepResult {
		const events = this.engine.step(command);
		return { events, achievements: this.afterStep(events) };
	}

	private afterStep(events: readonly Event[]): AchievementDef[] {
		const now = formatTimestamp(this.context.clock.now());
		const unlocked = this.profile.observe(events, this.state, now, this.info.runId);
		let profileChanged = unlocked.length > 0 || events.some((evt) => evt.type === "monster_killed");
		if (this.state.phase === "merchant") {
			this.#merchantSnapshot = [this.state.clone(), this.engine.rngState];
			this.writeSave();
		} else if (this.state.phase === "game_over" && this.finishedRecord === null) {
			this.finish();
			profileChanged = true;
		}
		if (profileChanged) this.context.repositories.profile.save(this.profile.profile);
		return unlocked;
	}

	/** Persists play time. Mid-battle quits resume from the last merchant visit. */
	saveAndQuit(): void {
		if (this.state.phase !== "game_over") this.writeSave();
	}

	// ── persistence ───────────────────────────────────────────────────────────

	private accumulatePlayTime(): Date {
		const now = this.context.clock.now();
		this.info.playTimeSeconds += Math.max(0, Math.floor((now.getTime() - this.#segmentStarted.getTime()) / 1000));
		this.#segmentStarted = now;
		return now;
	}

	private writeSave(): void {
		if (this.#merchantSnapshot === null) return;
		const now = this.accumulatePlayTime();
		const [run, rngState] = this.#merchantSnapshot;
		this.context.repositories.saves.save(
			new SaveGame(this.context.gameVersion, IMPLEMENTATION, formatTimestamp(now), rngState, this.info, run),
		);
	}

	private finish(): void {
		const now = formatTimestamp(this.accumulatePlayTime());
		const state = this.state;
		const record: RunRecord = {
			runId: this.info.runId,
			name: state.config.name,
			vocation: state.config.vocationId,
			difficulty: state.config.difficultyId,
			seed: state.seed,
			implementation: IMPLEMENTATION,
			gameVersion: this.context.gameVersion,
			startedAt: this.info.startedAt,
			endedAt: now,
			playTimeSeconds: this.info.playTimeSeconds,
			sessions: this.info.sessions,
			round: state.round,
			level: state.player.level,
			magicLevel: state.player.magicLevel,
			deathCause: state.deathCause ?? "",
			stats: state.stats,
		};
		this.context.repositories.history.add(record);
		this.context.repositories.saves.delete();
		this.profile.recordFinishedRun({
			runId: record.runId,
			name: record.name,
			vocation: record.vocation,
			difficulty: record.difficulty,
			round: record.round,
			level: record.level,
			endedAt: record.endedAt,
		});
		this.finishedRecord = record;
	}
}
