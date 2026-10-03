/** Merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10). */
import { buildSheet, itemValue, requiredLevel } from "../domain/character";
import type { GameData } from "../domain/definitions";
import type { ItemInstance } from "../domain/entities";
import type { Slot } from "../domain/enums";
import { pct, roundInfo } from "../domain/formulas";
import type { Rng } from "../domain/rng";
import { autoEquip } from "./auto-equip";
import type { MerchantCommand } from "./commands";
import { ErrorCode, type Event, error, event } from "./events";
import { canUse, generateItem } from "./loot";
import type { RunState } from "./run-state";

const MAX_POTIONS_PER_PURCHASE = 99;

export function stockPrice(item: ItemInstance, data: GameData): number {
	return pct(itemValue(item, data), data.balance.merchantMarkupPct);
}

export function availablePotions(state: RunState, data: GameData): string[] {
	const nextRound = state.round + 1;
	return data.potions.filter((potion) => potion.unlockRound <= nextRound).map((potion) => potion.id);
}

export class Merchant {
	constructor(
		private readonly data: GameData,
		private readonly rng: Rng,
		private readonly state: RunState,
	) {}

	/** Generates the rotating stock for the tier of the next round. */
	enter(): Event[] {
		const state = this.state;
		const vocation = this.data.vocation(state.player.vocationId);
		const tier = roundInfo(state.round + 1, this.data.balance, this.data.tierCount).tier;
		state.merchantStock = [];
		for (let i = 0; i < this.data.balance.merchantStockSize; i++) {
			const item = generateItem(this.data, this.rng, {
				vocation,
				tier,
				weights: this.data.balance.rarityWeights.merchant ?? {},
				uid: state.nextItemUid,
			});
			if (item !== null) {
				state.takeItemUid();
				state.merchantStock.push(item);
			}
		}
		return [event("merchant_entered", { round: state.round })];
	}

	handle(command: MerchantCommand): Event[] {
		switch (command.type) {
			case "buy_potion":
				return this.buyPotion(command.potionId, command.quantity);
			case "sell_item":
				return this.sell(command.uid);
			case "equip":
				return this.equip(command.uid);
			case "unequip":
				return this.unequip(command.slot);
			case "buy_stock_item":
				return this.buyStock(command.index);
		}
	}

	private buyPotion(potionId: string, quantity: number): Event[] {
		const player = this.state.player;
		if (!this.data.potions.some((potion) => potion.id === potionId)) return [error(ErrorCode.UNKNOWN_POTION)];
		if (!availablePotions(this.state, this.data).includes(potionId)) return [error(ErrorCode.POTION_LOCKED)];
		if (quantity < 1 || quantity > MAX_POTIONS_PER_PURCHASE) return [error(ErrorCode.INVALID_QUANTITY)];
		const cost = this.data.potion(potionId).price * quantity;
		if (player.gold < cost) return [error(ErrorCode.NOT_ENOUGH_GOLD)];
		player.gold -= cost;
		player.potions.set(potionId, player.potionCount(potionId) + quantity);
		return [event("potion_bought", { potionId, quantity, gold: cost })];
	}

	private findInBag(uid: number): ItemInstance | undefined {
		return this.state.player.bag.find((item) => item.uid === uid);
	}

	private removeFromBag(item: ItemInstance): void {
		const bag = this.state.player.bag;
		bag.splice(bag.indexOf(item), 1);
	}

	private sell(uid: number): Event[] {
		const item = this.findInBag(uid);
		if (item === undefined) return [error(ErrorCode.INVALID_ITEM)];
		const value = itemValue(item, this.data);
		this.removeFromBag(item);
		this.state.player.gold += value;
		return [event("item_sold", { uid, itemId: item.itemId, gold: value })];
	}

	private equip(uid: number): Event[] {
		const player = this.state.player;
		const item = this.findInBag(uid);
		if (item === undefined) return [error(ErrorCode.INVALID_ITEM)];
		const definition = this.data.item(item.itemId);
		if (!canUse(definition, this.data.vocation(player.vocationId))) return [error(ErrorCode.CANNOT_EQUIP)];
		if (requiredLevel(item, this.data) > player.level) return [error(ErrorCode.LEVEL_TOO_LOW)];
		const events: Event[] = [];
		this.removeFromBag(item);
		const previous = player.equipment.get(definition.slot);
		if (previous !== undefined) {
			player.equipment.delete(definition.slot);
			player.bag.push(previous);
			events.push(
				event("item_unequipped", { uid: previous.uid, itemId: previous.itemId, slot: definition.slot }),
			);
		}
		player.equipment.set(definition.slot, item);
		events.push(event("item_equipped", { uid: item.uid, itemId: item.itemId, slot: definition.slot }));
		this.clampResources();
		return events;
	}

	private unequip(slot: Slot): Event[] {
		const player = this.state.player;
		const item = player.equipment.get(slot);
		if (item === undefined) return [error(ErrorCode.INVALID_ITEM)];
		if (player.bag.length >= this.data.balance.bagCapacity) return [error(ErrorCode.BAG_FULL)];
		player.equipment.delete(slot);
		player.bag.push(item);
		this.clampResources();
		return [event("item_unequipped", { uid: item.uid, itemId: item.itemId, slot })];
	}

	private buyStock(index: number): Event[] {
		const state = this.state;
		const item = state.merchantStock[index];
		if (item === undefined || index < 0) return [error(ErrorCode.INVALID_ITEM)];
		if (state.player.bag.length >= this.data.balance.bagCapacity) return [error(ErrorCode.BAG_FULL)];
		const price = stockPrice(item, this.data);
		if (state.player.gold < price) return [error(ErrorCode.NOT_ENOUGH_GOLD)];
		state.player.gold -= price;
		state.merchantStock.splice(index, 1);
		state.player.bag.push(item);
		const events = [event("item_bought", { uid: item.uid, itemId: item.itemId, gold: price })];
		if (state.config.autoEquip) events.push(...autoEquip(state, this.data));
		return events;
	}

	private clampResources(): void {
		const player = this.state.player;
		const sheet = buildSheet(player, this.data);
		player.hp = Math.min(player.hp, sheet.maxHp);
		player.mp = Math.min(player.mp, sheet.maxMp);
	}
}
