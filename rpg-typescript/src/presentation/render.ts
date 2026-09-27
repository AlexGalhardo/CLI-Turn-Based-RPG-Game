/** Framework-independent rendering helpers (bars, colours, list keys) — specified in docs/tui.md. */
import type { Element } from "../domain/enums";

export const BAR_WIDTH = 25;
export const MIN_COLUMNS = 100;
export const MIN_ROWS = 30;
const LIST_KEYS = "123456789abcdefghijklmnopqrstuvwxyz";

/** Terminal colour names understood by Ink (chalk). */
export const ELEMENT_COLORS: Readonly<Record<Element, string>> = {
	physical: "white",
	fire: "red",
	ice: "cyan",
	energy: "magenta",
	earth: "green",
	holy: "yellow",
	death: "gray",
};

export const RARITY_COLORS: Readonly<Record<string, string>> = {
	common: "white",
	rare: "#1e90ff",
	epic: "#af87ff",
	legendary: "#ffaf00",
};

/** `█` filled / `░` empty. A living creature always shows at least one filled cell. */
export function bar(current: number, maximum: number, width = BAR_WIDTH): string {
	if (maximum <= 0) return "░".repeat(width);
	let filled = Math.floor((width * Math.max(0, Math.min(current, maximum))) / maximum);
	if (current > 0) filled = Math.max(1, filled);
	return "█".repeat(filled) + "░".repeat(width - filled);
}

export function hpColor(current: number, maximum: number): string {
	if (maximum > 0 && current * 100 > maximum * 50) return "green";
	if (maximum > 0 && current * 100 > maximum * 25) return "yellow";
	return "red";
}

export function listKey(index: number): string {
	const key = LIST_KEYS[index];
	if (key === undefined) throw new RangeError(`no list key for index ${index}`);
	return key;
}

export function listIndex(key: string): number | null {
	const position = key.length === 1 ? LIST_KEYS.indexOf(key) : -1;
	return position < 0 ? null : position;
}
