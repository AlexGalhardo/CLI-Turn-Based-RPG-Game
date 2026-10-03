/** Turns engine events into translated sentences. Shared by every presentation. */
import type { Event, EventValue } from "../application/events";
import type { RunState } from "../application/run-state";
import { type GameData, UnknownIdError } from "../domain/definitions";
import type { Translator } from "../infrastructure/i18n";

// Event fields that hold ids: they are replaced by display names before formatting.
const NAME_FIELDS: ReadonlyArray<readonly [string, string]> = [
	["spellId", "spell"],
	["potionId", "potion"],
	["monsterId", "monster"],
	["itemId", "item"],
];

export class EventFormatter {
	constructor(
		private readonly data: GameData,
		private readonly translator: Translator,
	) {}

	format(evt: Event, state: RunState): string {
		const params: Record<string, unknown> = { ...evt };
		for (const [fieldName, name] of NAME_FIELDS) {
			const value = evt[fieldName];
			if (value !== undefined) params[name] = this.displayName(fieldName, value);
		}
		for (const fieldName of ["element", "status", "rarity", "resource"]) {
			const value = evt[fieldName];
			if (value !== undefined) params[fieldName] = this.translator.t(`${fieldName}.${String(value)}`);
		}
		if (!("monster" in params) && state.monster !== null) {
			params.monster = this.data.creature(state.monster.creatureId).name;
		}
		const uid = evt.uid;
		if (uid !== undefined && !("item" in params)) params.item = this.itemNameByUid(uid, state);
		return this.translator.t(this.key(evt), params);
	}

	private key(evt: Event): string {
		if (evt.type === "error") return `error.${String(evt.code)}`;
		let variant = "";
		if (evt.crit === true) variant = "_crit";
		else if (evt.charged === true) variant = "_charged";
		else if (evt.type === "round_started" && evt.isBoss === true) variant = "_boss";
		else if (evt.type === "round_started" && evt.enemyClass === "elite") variant = "_elite";
		else if (evt.target !== undefined) variant = `_${String(evt.target)}`;
		return `event.${evt.type}${variant}`;
	}

	private displayName(fieldName: string, value: EventValue): string {
		const identifier = String(value);
		try {
			switch (fieldName) {
				case "spellId":
					return this.data.spell(identifier).name;
				case "potionId":
					return this.data.potion(identifier).name;
				case "monsterId":
					return this.data.creature(identifier).name;
				default:
					return this.data.item(identifier).name;
			}
		} catch (exc) {
			if (exc instanceof UnknownIdError) return identifier;
			throw exc;
		}
	}

	private itemNameByUid(uid: EventValue, state: RunState): string {
		const player = state.player;
		for (const item of [...player.bag, ...player.equipment.values(), ...state.merchantStock]) {
			if (item.uid === uid) return this.data.item(item.itemId).name;
		}
		return `#${String(uid)}`;
	}
}
