/** Auto-equip with auto-sell (docs/game-design.md §8.1). Consumes no randomness. */
import { buildSheet, itemScore, itemValue, requiredLevel } from "../domain/character";
import type { GameData } from "../domain/definitions";
import type { ItemInstance } from "../domain/entities";
import { EQUIPMENT_SLOT_ORDER, type Slot } from "../domain/enums";
import { type Event, event } from "./events";
import { canUse } from "./loot";
import type { RunState } from "./run-state";

/** Highest-score bag item the player can wear in `slot` now; ties go to the lowest uid. */
export function bestBagItem(state: RunState, data: GameData, slot: Slot): ItemInstance | null {
	const player = state.player;
	const vocation = data.vocation(player.vocationId);
	let best: ItemInstance | null = null;
	let bestScore = 0;
	for (const item of player.bag) {
		const definition = data.item(item.itemId);
		if (definition.slot !== slot || !canUse(definition, vocation) || requiredLevel(item, data) > player.level) {
			continue;
		}
		const score = itemScore(item, data);
		if (best === null || score > bestScore || (score === bestScore && item.uid < best.uid)) {
			best = item;
			bestScore = score;
		}
	}
	return best;
}

export function autoEquip(state: RunState, data: GameData): Event[] {
	const player = state.player;
	const events: Event[] = [];
	for (const slot of EQUIPMENT_SLOT_ORDER) {
		const best = bestBagItem(state, data, slot);
		if (best === null) continue;
		const score = itemScore(best, data);
		const current = player.equipment.get(slot);
		if (current !== undefined && score <= itemScore(current, data)) continue;
		player.bag.splice(player.bag.indexOf(best), 1);
		player.equipment.set(slot, best);
		events.push(event("item_auto_equipped", { uid: best.uid, itemId: best.itemId, slot, score }));
		if (current !== undefined) {
			const gold = itemValue(current, data);
			player.gold += gold;
			events.push(event("item_auto_sold", { uid: current.uid, itemId: current.itemId, gold }));
		}
	}
	if (events.length > 0) {
		const sheet = buildSheet(player, data);
		player.hp = Math.min(player.hp, sheet.maxHp);
		player.mp = Math.min(player.mp, sheet.maxMp);
	}
	return events;
}
