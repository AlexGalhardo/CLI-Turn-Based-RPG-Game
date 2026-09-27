/** Player commands. Each maps 1:1 to a JSON object used by golden files (`{"type": "cast", "spellId": "…"}`). */
import { parseEnum, SLOTS, type Slot } from "../domain/enums";
import { field, type JsonObject, type JsonValue, jsonInt, jsonObj, jsonStr } from "../domain/json-types";

export type Command =
	| { readonly type: "attack" }
	| { readonly type: "cast"; readonly spellId: string }
	| { readonly type: "potion"; readonly potionId: string }
	| { readonly type: "defend" }
	| { readonly type: "next_fight" }
	| { readonly type: "buy_potion"; readonly potionId: string; readonly quantity: number }
	| { readonly type: "sell_item"; readonly uid: number }
	| { readonly type: "equip"; readonly uid: number }
	| { readonly type: "unequip"; readonly slot: Slot }
	| { readonly type: "buy_stock_item"; readonly index: number };

export type BattleCommand = Extract<Command, { type: "attack" | "cast" | "potion" | "defend" }>;
export type MerchantCommand = Extract<
	Command,
	{ type: "buy_potion" | "sell_item" | "equip" | "unequip" | "buy_stock_item" }
>;

export const Attack = (): Command => ({ type: "attack" });
export const Cast = (spellId: string): Command => ({ type: "cast", spellId });
export const UsePotion = (potionId: string): Command => ({ type: "potion", potionId });
export const Defend = (): Command => ({ type: "defend" });
export const NextFight = (): Command => ({ type: "next_fight" });
export const BuyPotion = (potionId: string, quantity: number): Command => ({ type: "buy_potion", potionId, quantity });
export const SellItem = (uid: number): Command => ({ type: "sell_item", uid });
export const Equip = (uid: number): Command => ({ type: "equip", uid });
export const Unequip = (slot: Slot): Command => ({ type: "unequip", slot });
export const BuyStockItem = (index: number): Command => ({ type: "buy_stock_item", index });

export function commandToJson(command: Command): JsonObject {
	return { ...command };
}

export function commandFromJson(raw: JsonValue): Command {
	const data = jsonObj(raw);
	const type = jsonStr(field(data, "type"));
	switch (type) {
		case "attack":
			return Attack();
		case "cast":
			return Cast(jsonStr(field(data, "spellId")));
		case "potion":
			return UsePotion(jsonStr(field(data, "potionId")));
		case "defend":
			return Defend();
		case "next_fight":
			return NextFight();
		case "buy_potion":
			return BuyPotion(jsonStr(field(data, "potionId")), jsonInt(field(data, "quantity")));
		case "sell_item":
			return SellItem(jsonInt(field(data, "uid")));
		case "equip":
			return Equip(jsonInt(field(data, "uid")));
		case "unequip":
			return Unequip(parseEnum(SLOTS, jsonStr(field(data, "slot")), "slot"));
		case "buy_stock_item":
			return BuyStockItem(jsonInt(field(data, "index")));
		default:
			throw new TypeError(`unknown command type: ${type}`);
	}
}
