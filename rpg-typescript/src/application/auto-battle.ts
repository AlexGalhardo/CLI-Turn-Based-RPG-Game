/**
 * Auto-battle policy (docs/game-design.md §13): picks the player's battle commands from the run state only.
 *
 * It lives in the application layer, not in the engine: the commands it returns are ordinary commands, so a fight
 * played by the policy replays like any other. Every command it returns is valid (affordable spells, owned potions).
 */
import { buildSheet } from "../domain/character";
import type { AutoBattleDef, AutoBattleModeDef, GameData, PotionDef, SpellDef } from "../domain/definitions";
import type { Resource, SpellKind } from "../domain/enums";
import { pct, spellLevelForUses } from "../domain/formulas";
import type { BattleCommand } from "./commands";
import type { RunState } from "./run-state";

const OFFENSE_ATTACK = "attack";

export const AUTO_BATTLE_MODES = ["melee", "spells", "balanced"] as const;
export type AutoBattleMode = (typeof AUTO_BATTLE_MODES)[number];

/** Highest `max`; ties go to the lowest id. */
function strongest<T extends SpellDef | PotionDef>(options: readonly T[]): T | null {
	let best: T | null = null;
	for (const option of options) {
		if (best === null || option.max > best.max || (option.max === best.max && option.id < best.id)) {
			best = option;
		}
	}
	return best;
}

export class AutoBattlePolicy {
	readonly #mode: AutoBattleModeDef;
	readonly #config: AutoBattleDef;

	constructor(
		private readonly data: GameData,
		mode: AutoBattleMode,
	) {
		this.#mode = data.balance.autoBattle.mode(mode);
		this.#config = data.balance.autoBattle;
	}

	choose(state: RunState): BattleCommand {
		const player = state.player;
		const sheet = buildSheet(player, this.data);
		const config = this.#config;

		if (player.hp * 100 < sheet.maxHp * config.emergencyHealBelowPct) {
			const heal = this.heal(state);
			if (heal !== null) return heal;
		}
		const every = this.#mode.supportEvery;
		if (state.turn % every === every - 1) {
			if (player.hp * 100 < sheet.maxHp * config.healBelowPct) {
				const heal = this.heal(state);
				if (heal !== null) return heal;
			}
			if (player.mp * 100 < sheet.maxMp * config.manaBelowPct) {
				const potion = this.bestPotion(state, "mp");
				if (potion !== null) return { type: "potion", potionId: potion.id };
			}
			if (this.telegraphPending(state)) return { type: "defend" };
		}
		return this.offense(state);
	}

	private offense(state: RunState): BattleCommand {
		if (this.#mode.offense === OFFENSE_ATTACK) return { type: "attack" };
		const spell = strongest(this.affordable(state, "attack"));
		return spell === null ? { type: "attack" } : { type: "cast", spellId: spell.id };
	}

	private heal(state: RunState): BattleCommand | null {
		const spell = strongest(this.affordable(state, "heal"));
		if (spell !== null) return { type: "cast", spellId: spell.id };
		const potion = this.bestPotion(state, "hp");
		return potion === null ? null : { type: "potion", potionId: potion.id };
	}

	private affordable(state: RunState, kind: SpellKind): SpellDef[] {
		const player = state.player;
		const levels = this.data.balance.spellLevels;
		return this.data
			.vocation(player.vocationId)
			.spells.map((spellId) => this.data.spell(spellId))
			.filter(
				(spell) =>
					spell.kind === kind &&
					pct(spell.mana, spellLevelForUses(player.spellUses.get(spell.id) ?? 0, levels).manaPct) <=
						player.mp,
			);
	}

	private bestPotion(state: RunState, resource: Resource): PotionDef | null {
		return strongest(
			this.data.potions.filter((p) => p.resource === resource && state.player.potionCount(p.id) > 0),
		);
	}

	/** The boss announced its charged attack: its next action is the charge. */
	private telegraphPending(state: RunState): boolean {
		const monster = state.monster;
		if (monster === null || !monster.isBoss) return false;
		const every = this.data.balance.bossTelegraphEvery;
		return monster.bossActions % (every + 1) === every;
	}
}
