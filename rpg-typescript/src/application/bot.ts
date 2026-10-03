/**
 * A deterministic heuristic player used by the simulator and by the end-to-end parity tests.
 * Its decisions are part of the golden "bot full run" files: it mirrors the Python GreedyBot exactly,
 * including tie-breakers (max by `(value, id)`).
 */
import { buildSheet, itemScore, requiredLevel } from "../domain/character";
import type { GameData, PotionDef, SpellDef } from "../domain/definitions";
import type { MonsterInstance } from "../domain/entities";
import type { Resource, SpellKind } from "../domain/enums";
import { pct, spellLevelForUses } from "../domain/formulas";
import {
	Attack,
	BuyPotion,
	Cast,
	type Command,
	Defend,
	EndRun,
	Equip,
	NextFight,
	SellItem,
	UsePotion,
} from "./commands";
import { canUse } from "./loot";
import { availablePotions } from "./merchant";
import type { RunState } from "./run-state";

const HEAL_THRESHOLD_PCT = 45;
const MANA_POTION_THRESHOLD_PCT = 25;
const MAX_POTION_STOCK = 20;

/** Python's `max(items, key=...)` with tuple keys `(number, id)`: first maximum wins on full ties. */
function maxBy<T>(items: readonly T[], key: (item: T) => readonly [number, string]): T | undefined {
	let best: T | undefined;
	let bestKey: readonly [number, string] | undefined;
	for (const item of items) {
		const current = key(item);
		if (
			bestKey === undefined ||
			current[0] > bestKey[0] ||
			(current[0] === bestKey[0] && current[1] > bestKey[1])
		) {
			best = item;
			bestKey = current;
		}
	}
	return best;
}

export class GreedyBot {
	constructor(private readonly data: GameData) {}

	choose(state: RunState): Command {
		if (state.phase === "battle") return this.battle(state);
		if (state.phase === "victory") return EndRun();
		return this.merchant(state);
	}

	// ── battle ────────────────────────────────────────────────────────────────

	private battle(state: RunState): Command {
		const player = state.player;
		const monster = state.monster;
		if (monster === null) throw new Error("battle without a monster");
		const sheet = buildSheet(player, this.data);

		if (this.chargeIncoming(monster)) return Defend();
		if (player.hp * 100 < sheet.maxHp * HEAL_THRESHOLD_PCT) {
			const heal = this.heal(state);
			if (heal !== null) return heal;
		}
		if (player.mp * 100 < sheet.maxMp * MANA_POTION_THRESHOLD_PCT) {
			const potion = this.bestOwnedPotion(state, "mp");
			if (potion !== undefined) return UsePotion(potion.id);
		}
		const spell = this.bestAttackSpell(state, monster);
		return spell === undefined ? Attack() : Cast(spell.id);
	}

	private chargeIncoming(monster: MonsterInstance): boolean {
		if (!monster.isBoss) return false;
		const every = this.data.balance.bossTelegraphEvery;
		return monster.bossActions % (every + 1) === every;
	}

	private cost(state: RunState, spell: SpellDef): number {
		const uses = state.player.spellUses.get(spell.id) ?? 0;
		return pct(spell.mana, spellLevelForUses(uses, this.data.balance.spellLevels).manaPct);
	}

	private spells(state: RunState, kind: SpellKind): SpellDef[] {
		const vocation = this.data.vocation(state.player.vocationId);
		return vocation.spells
			.map((id) => this.data.spell(id))
			.filter((spell) => spell.kind === kind && this.cost(state, spell) <= state.player.mp);
	}

	private heal(state: RunState): Command | null {
		const spell = maxBy(this.spells(state, "heal"), (s) => [s.max, s.id]);
		if (spell !== undefined) return Cast(spell.id);
		const potion = this.bestOwnedPotion(state, "hp");
		return potion === undefined ? null : UsePotion(potion.id);
	}

	private bestOwnedPotion(state: RunState, resource: Resource): PotionDef | undefined {
		const owned = this.data.potions.filter((p) => p.resource === resource && state.player.potionCount(p.id) > 0);
		return maxBy(owned, (p) => [p.max, p.id]);
	}

	private bestAttackSpell(state: RunState, monster: MonsterInstance): SpellDef | undefined {
		const creature = this.data.creature(monster.creatureId);
		const candidates = this.spells(state, "attack").filter((s) => creature.resistance(s.element) > 0);
		return maxBy(candidates, (s) => [(s.min + s.max) * creature.resistance(s.element), s.id]);
	}

	// ── merchant ──────────────────────────────────────────────────────────────

	private merchant(state: RunState): Command {
		const player = state.player;
		const vocation = this.data.vocation(player.vocationId);
		for (const item of [...player.bag].sort((a, b) => a.uid - b.uid)) {
			const definition = this.data.item(item.itemId);
			if (!canUse(definition, vocation) || requiredLevel(item, this.data) > player.level) continue;
			const current = player.equipment.get(definition.slot);
			if (current === undefined || itemScore(item, this.data) > itemScore(current, this.data)) {
				return Equip(item.uid);
			}
		}
		if (player.bag.length > 0) {
			return SellItem(Math.min(...player.bag.map((item) => item.uid)));
		}
		return this.potionPurchase(state, "hp") ?? this.potionPurchase(state, "mp") ?? NextFight();
	}

	private potionPurchase(state: RunState, resource: Resource): Command | null {
		const unlocked = new Set(availablePotions(state, this.data));
		const options = this.data.potions.filter((p) => p.resource === resource && unlocked.has(p.id));
		const best = maxBy(options, (p) => [p.max, p.id]);
		if (best === undefined) return null;
		const owned = options.reduce((sum, p) => sum + state.player.potionCount(p.id), 0);
		const target = Math.min(MAX_POTION_STOCK, 5 + Math.floor(state.round / 5));
		const budget = resource === "mp" ? Math.floor(state.player.gold / 2) : state.player.gold;
		const quantity = Math.min(target - owned, Math.floor(budget / best.price));
		return quantity <= 0 ? null : BuyPotion(best.id, quantity);
	}
}
