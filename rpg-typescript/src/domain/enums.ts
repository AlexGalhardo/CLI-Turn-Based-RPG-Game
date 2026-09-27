export const ELEMENTS = ["physical", "fire", "ice", "energy", "earth", "holy", "death"] as const;
export type Element = (typeof ELEMENTS)[number];

export const SLOTS = ["helmet", "armor", "legs", "boots", "amulet", "ring", "weapon", "shield"] as const;
export type Slot = (typeof SLOTS)[number];

export const PHASES = ["merchant", "battle", "game_over"] as const;
export type Phase = (typeof PHASES)[number];

export type Resource = "hp" | "mp";
export type SpellKind = "attack" | "heal";
export type StatusKind = "dot" | "stun";
export type Target = "player" | "monster";

export const STATS = [
	"attack",
	"armor",
	"maxHp",
	"maxMp",
	"hpRegen",
	"mpRegen",
	"critChance",
	"critDamage",
	"spellPower",
	"physicalDamage",
	"dodge",
	"parry",
	"lifeLeech",
	"manaLeech",
	"protPhysical",
	"protFire",
	"protIce",
	"protEnergy",
	"protEarth",
	"protHoly",
	"protDeath",
] as const;
export type Stat = (typeof STATS)[number];

export const PROTECTION_BY_ELEMENT: Readonly<Record<Element, Stat>> = {
	physical: "protPhysical",
	fire: "protFire",
	ice: "protIce",
	energy: "protEnergy",
	earth: "protEarth",
	holy: "protHoly",
	death: "protDeath",
};

function isOneOf<T extends string>(values: readonly T[], value: string): value is T {
	return (values as readonly string[]).includes(value);
}

export function parseEnum<T extends string>(values: readonly T[], value: string, what: string): T {
	if (!isOneOf(values, value)) {
		throw new TypeError(`invalid ${what}: ${value}`);
	}
	return value;
}
