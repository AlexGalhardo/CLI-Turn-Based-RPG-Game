/** Experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9). */
import { buildSheet } from "../domain/character";
import type { GameData, SpellDef } from "../domain/definitions";
import type { Player } from "../domain/entities";
import { manaForMagicLevel, spellLevelForUses, xpForLevel } from "../domain/formulas";
import { type Event, event } from "./events";

export class Progression {
	constructor(private readonly data: GameData) {}

	gainExperience(player: Player, amount: number): Event[] {
		player.xp += amount;
		const events: Event[] = [event("xp_gained", { amount, total: player.xp })];
		const vocation = this.data.vocation(player.vocationId);
		while (player.xp >= xpForLevel(player.level + 1)) {
			player.level += 1;
			const sheet = buildSheet(player, this.data);
			player.hp = Math.min(sheet.maxHp, player.hp + vocation.hpPerLevel);
			player.mp = Math.min(sheet.maxMp, player.mp + vocation.mpPerLevel);
			events.push(event("level_up", { level: player.level, maxHp: sheet.maxHp, maxMp: sheet.maxMp }));
		}
		return events;
	}

	afterCast(player: Player, spell: SpellDef, manaCost: number): Event[] {
		const levels = this.data.balance.spellLevels;
		const events: Event[] = [];
		const usesBefore = player.spellUses.get(spell.id) ?? 0;
		player.spellUses.set(spell.id, usesBefore + 1);
		const before = spellLevelForUses(usesBefore, levels);
		const after = spellLevelForUses(usesBefore + 1, levels);
		if (after.level !== before.level) {
			events.push(event("spell_level_up", { spellId: spell.id, level: after.level }));
		}
		player.manaSpent += manaCost;
		while (player.manaSpent >= manaForMagicLevel(player.magicLevel, this.data.balance)) {
			player.magicLevel += 1;
			events.push(event("magic_level_up", { magicLevel: player.magicLevel }));
		}
		return events;
	}
}
