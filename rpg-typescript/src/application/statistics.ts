/** Deterministic run counters, derived only from engine events (docs/game-design.md §11). */
import { field, type JsonObject, type JsonValue, jsonInt, jsonList, jsonObj, jsonStr } from "../domain/json-types";
import { type Event, intField, strField } from "./events";

export interface DroppedItem {
	readonly itemId: string;
	readonly rarity: string;
	readonly round: number;
}

export class Counter {
	readonly #counts = new Map<string, number>();

	add(key: string, amount = 1): void {
		this.#counts.set(key, this.get(key) + amount);
	}

	get(key: string): number {
		return this.#counts.get(key) ?? 0;
	}

	values(): number[] {
		return [...this.#counts.values()];
	}

	entries(): Array<[string, number]> {
		return [...this.#counts.entries()];
	}

	total(): number {
		return this.values().reduce((sum, value) => sum + value, 0);
	}

	toJson(): JsonObject {
		const result: JsonObject = {};
		for (const key of [...this.#counts.keys()].sort()) result[key] = this.get(key);
		return result;
	}

	static fromJson(raw: JsonValue): Counter {
		const counter = new Counter();
		for (const [key, value] of Object.entries(jsonObj(raw))) counter.add(key, jsonInt(value));
		return counter;
	}
}

export class RunStatistics {
	damageDealt = 0;
	damageTaken = 0;
	healingDone = 0;
	highestHit = 0;
	normalAttacks = 0;
	crits = 0;
	dodges = 0;
	parries = 0;
	defends = 0;
	goldLooted = 0;
	goldSpent = 0;
	goldEarned = 0;
	itemsSold = 0;
	bossesKilled = 0;
	spellsCast = new Counter();
	potionsUsed = new Counter();
	potionsBought = new Counter();
	itemsDropped = new Counter();
	kills = new Counter();
	statusesApplied = new Counter();
	droppedItems: DroppedItem[] = [];

	record(events: readonly Event[], currentRound: number): void {
		for (const evt of events) this.#recordOne(evt, currentRound);
	}

	#recordOne(evt: Event, currentRound: number): void {
		switch (evt.type) {
			case "player_attacked":
				this.normalAttacks += 1;
				this.#dealt(evt);
				break;
			case "spell_cast":
				this.spellsCast.add(strField(evt, "spellId"));
				this.#dealt(evt);
				break;
			case "spell_healed":
				this.spellsCast.add(strField(evt, "spellId"));
				this.healingDone += intField(evt, "amount");
				break;
			case "potion_used":
				this.potionsUsed.add(strField(evt, "potionId"));
				if (evt.resource === "hp") this.healingDone += intField(evt, "amount");
				break;
			case "player_defended":
				this.defends += 1;
				break;
			case "monster_attacked":
				this.damageTaken += intField(evt, "damage");
				break;
			case "attack_dodged":
				this.dodges += 1;
				break;
			case "attack_parried":
				this.parries += 1;
				break;
			case "status_ticked":
				if (evt.target === "player") this.damageTaken += intField(evt, "damage");
				else this.damageDealt += intField(evt, "damage");
				break;
			case "status_applied":
				if (evt.target === "monster") this.statusesApplied.add(strField(evt, "status"));
				break;
			case "monster_killed":
				this.kills.add(strField(evt, "monsterId"));
				if (evt.isBoss === true) this.bossesKilled += 1;
				break;
			case "gold_looted":
				this.goldLooted += intField(evt, "amount");
				break;
			case "item_dropped": {
				const rarity = strField(evt, "rarity");
				this.itemsDropped.add(rarity);
				this.droppedItems.push({ itemId: strField(evt, "itemId"), rarity, round: currentRound });
				break;
			}
			case "potion_bought":
				this.potionsBought.add(strField(evt, "potionId"), intField(evt, "quantity"));
				this.goldSpent += intField(evt, "gold");
				break;
			case "item_bought":
				this.goldSpent += intField(evt, "gold");
				break;
			case "item_sold":
			case "item_auto_sold":
				this.itemsSold += 1;
				this.goldEarned += intField(evt, "gold");
				break;
			default:
				break;
		}
	}

	#dealt(evt: Event): void {
		const damage = intField(evt, "damage");
		this.damageDealt += damage;
		this.highestHit = Math.max(this.highestHit, damage);
		if (evt.crit === true) this.crits += 1;
	}

	toJson(): JsonObject {
		return {
			damageDealt: this.damageDealt,
			damageTaken: this.damageTaken,
			healingDone: this.healingDone,
			highestHit: this.highestHit,
			normalAttacks: this.normalAttacks,
			crits: this.crits,
			dodges: this.dodges,
			parries: this.parries,
			defends: this.defends,
			goldLooted: this.goldLooted,
			goldSpent: this.goldSpent,
			goldEarned: this.goldEarned,
			itemsSold: this.itemsSold,
			bossesKilled: this.bossesKilled,
			spellsCast: this.spellsCast.toJson(),
			potionsUsed: this.potionsUsed.toJson(),
			potionsBought: this.potionsBought.toJson(),
			itemsDropped: this.itemsDropped.toJson(),
			kills: this.kills.toJson(),
			statusesApplied: this.statusesApplied.toJson(),
			droppedItems: this.droppedItems.map((item) => ({
				itemId: item.itemId,
				rarity: item.rarity,
				round: item.round,
			})),
		};
	}

	static fromJson(raw: JsonValue): RunStatistics {
		const data = jsonObj(raw);
		const stats = new RunStatistics();
		stats.damageDealt = jsonInt(field(data, "damageDealt"));
		stats.damageTaken = jsonInt(field(data, "damageTaken"));
		stats.healingDone = jsonInt(field(data, "healingDone"));
		stats.highestHit = jsonInt(field(data, "highestHit"));
		stats.normalAttacks = jsonInt(field(data, "normalAttacks"));
		stats.crits = jsonInt(field(data, "crits"));
		stats.dodges = jsonInt(field(data, "dodges"));
		stats.parries = jsonInt(field(data, "parries"));
		stats.defends = jsonInt(field(data, "defends"));
		stats.goldLooted = jsonInt(field(data, "goldLooted"));
		stats.goldSpent = jsonInt(field(data, "goldSpent"));
		stats.goldEarned = jsonInt(field(data, "goldEarned"));
		stats.itemsSold = jsonInt(field(data, "itemsSold"));
		stats.bossesKilled = jsonInt(field(data, "bossesKilled"));
		stats.spellsCast = Counter.fromJson(field(data, "spellsCast"));
		stats.potionsUsed = Counter.fromJson(field(data, "potionsUsed"));
		stats.potionsBought = Counter.fromJson(field(data, "potionsBought"));
		stats.itemsDropped = Counter.fromJson(field(data, "itemsDropped"));
		stats.kills = Counter.fromJson(field(data, "kills"));
		stats.statusesApplied = Counter.fromJson(field(data, "statusesApplied"));
		stats.droppedItems = jsonList(field(data, "droppedItems")).map((entry) => {
			const item = jsonObj(entry);
			return {
				itemId: jsonStr(field(item, "itemId")),
				rarity: jsonStr(field(item, "rarity")),
				round: jsonInt(field(item, "round")),
			};
		});
		return stats;
	}
}
