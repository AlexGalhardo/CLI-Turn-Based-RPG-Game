/** Engine events (docs/cross-language-parity.md §3). Flat JSON objects compared across implementations. */
export type EventValue = number | string | boolean;
export type Event = { readonly type: string } & { readonly [field: string]: EventValue };

export function event(type: string, fields: Readonly<Record<string, EventValue>> = {}): Event {
	return { type, ...fields };
}

export function error(code: string): Event {
	return event("error", { code });
}

export const ErrorCode = {
	NOT_ENOUGH_MANA: "not_enough_mana",
	NOT_ENOUGH_GOLD: "not_enough_gold",
	NO_POTION: "no_potion",
	UNKNOWN_SPELL: "unknown_spell",
	UNKNOWN_POTION: "unknown_potion",
	POTION_LOCKED: "potion_locked",
	INVALID_PHASE: "invalid_phase",
	INVALID_QUANTITY: "invalid_quantity",
	BAG_FULL: "bag_full",
	CANNOT_EQUIP: "cannot_equip",
	INVALID_ITEM: "invalid_item",
	LEVEL_TOO_LOW: "level_too_low",
	UNKNOWN_COMMAND: "unknown_command",
} as const;

export function intField(evt: Event, name: string): number {
	const value = evt[name];
	if (typeof value !== "number") throw new TypeError(`event field ${name} must be int`);
	return value;
}

export function strField(evt: Event, name: string): string {
	const value = evt[name];
	if (typeof value !== "string") throw new TypeError(`event field ${name} must be str`);
	return value;
}
