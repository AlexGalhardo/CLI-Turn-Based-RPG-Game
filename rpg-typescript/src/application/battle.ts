/** Battle resolution, following docs/game-design.md §6. Every RNG call here is part of the contract. */
import { buildSheet, type CharacterSheet, protection } from "../domain/character";
import type { EnemyClassDef, GameData, MonsterAttack, SpellDef } from "../domain/definitions";
import type { ActiveStatus, MonsterInstance, Player } from "../domain/entities";
import type { Element, Target } from "../domain/enums";
import { armorMitigation, pct, spellLevelForUses } from "../domain/formulas";
import type { Rng } from "../domain/rng";
import type { BattleCommand } from "./commands";
import { ErrorCode, type Event, error, event } from "./events";
import { Progression } from "./progression";
import type { RunState } from "./run-state";

const STUN = "stun";
const STUN_COOLDOWN_TURNS = 2;

export type BattleOutcome = "ongoing" | "victory" | "defeat";

export class Battle {
	readonly #progression: Progression;

	constructor(
		private readonly data: GameData,
		private readonly rng: Rng,
		private readonly state: RunState,
	) {
		this.#progression = new Progression(data);
	}

	private get player(): Player {
		return this.state.player;
	}

	private get monster(): MonsterInstance {
		const monster = this.state.monster;
		if (monster === null) throw new Error("battle without a monster");
		return monster;
	}

	private sheet(): CharacterSheet {
		return buildSheet(this.player, this.data);
	}

	private enemyClass(): EnemyClassDef {
		return this.data.balance.enemyClass(this.monster.enemyClass);
	}

	// ── validation ────────────────────────────────────────────────────────────

	/** Returns an error event for an invalid command. Validation never consumes randomness. */
	validate(command: BattleCommand): Event | null {
		if (command.type === "cast") {
			const vocation = this.data.vocation(this.player.vocationId);
			if (!vocation.spells.includes(command.spellId)) return error(ErrorCode.UNKNOWN_SPELL);
			if (this.player.mp < this.spellCost(this.data.spell(command.spellId)))
				return error(ErrorCode.NOT_ENOUGH_MANA);
		} else if (command.type === "potion") {
			if (!this.data.potions.some((potion) => potion.id === command.potionId))
				return error(ErrorCode.UNKNOWN_POTION);
			if (this.player.potionCount(command.potionId) <= 0) return error(ErrorCode.NO_POTION);
		}
		return null;
	}

	private spellCost(spell: SpellDef): number {
		const level = spellLevelForUses(this.player.spellUses.get(spell.id) ?? 0, this.data.balance.spellLevels);
		return pct(spell.mana, level.manaPct);
	}

	// ── turn ──────────────────────────────────────────────────────────────────

	playTurn(command: BattleCommand): [Event[], BattleOutcome] {
		const events: Event[] = [];
		this.playerAction(command, events);
		for (;;) {
			let outcome = this.deathCheck();
			if (outcome !== null) return [events, outcome];
			this.monsterPhase(events);
			outcome = this.deathCheck();
			if (outcome !== null) return [events, outcome];
			if (this.endOfTurn(events)) return [events, "defeat"];
			if (!Battle.consumeStun(this.player.statuses)) return [events, "ongoing"];
			this.player.stunCooldown = STUN_COOLDOWN_TURNS;
			events.push(event("player_stunned"));
		}
	}

	/** Steps 2 and 4: a parried hit can kill the attacker, so the player is checked first. */
	private deathCheck(): BattleOutcome | null {
		if (this.player.hp <= 0) return "defeat";
		if (this.monster.hp <= 0) return "victory";
		return null;
	}

	// ── step 1: player action ─────────────────────────────────────────────────

	private playerAction(command: BattleCommand, events: Event[]): void {
		switch (command.type) {
			case "attack":
				this.melee(events);
				break;
			case "cast":
				this.cast(this.data.spell(command.spellId), events);
				break;
			case "potion":
				this.drink(command.potionId, events);
				break;
			case "defend":
				this.player.defending = true;
				events.push(event("player_defended"));
				break;
		}
	}

	private melee(events: Event[]): void {
		const sheet = this.sheet();
		if (this.monsterDodges(events)) return;
		const base = pct(this.rng.roll(sheet.meleeMin, sheet.meleeMax), 100 + sheet.physicalDamage);
		const [rolled, crit] = this.rollCrit(base, sheet);
		const damage = this.resisted(rolled, sheet.weaponElement);
		if (this.monsterParries(damage, sheet.weaponElement, events)) return;
		this.hitMonster(damage);
		events.push(event("player_attacked", { damage, crit, element: sheet.weaponElement }));
		this.leech(damage, sheet, events);
	}

	private monsterDodges(events: Event[]): boolean {
		if (!this.rng.chance(this.enemyClass().dodge)) return false;
		events.push(event("monster_dodged"));
		return true;
	}

	/** Physical hits only: the monster takes nothing and reflects part of the hit (no mitigation). */
	private monsterParries(damage: number, element: Element, events: Event[]): boolean {
		if (element !== "physical" || !this.rng.chance(this.enemyClass().parry)) return false;
		const reflected = Math.max(1, pct(damage, this.data.balance.parryReflectPct));
		this.player.hp = Math.max(0, this.player.hp - reflected);
		events.push(event("monster_parried", { reflected }));
		return true;
	}

	private cast(spell: SpellDef, events: Event[]): void {
		const player = this.player;
		const sheet = this.sheet();
		const level = spellLevelForUses(player.spellUses.get(spell.id) ?? 0, this.data.balance.spellLevels);
		const cost = pct(spell.mana, level.manaPct);
		player.mp -= cost;
		if (spell.kind === "attack" && this.monsterDodges(events)) {
			events.push(...this.#progression.afterCast(player, spell, cost));
			return;
		}
		const bonus = player.level * spell.perLevel + player.magicLevel * spell.perMagicLevel;
		const amount = pct(
			pct(this.rng.roll(spell.min + bonus, spell.max + bonus), level.effectPct),
			100 + sheet.spellPower,
		);

		if (spell.kind === "attack") {
			let [damage, crit] = this.rollCrit(amount, sheet);
			damage = this.resisted(damage, spell.element);
			if (this.monsterParries(damage, spell.element, events)) {
				events.push(...this.#progression.afterCast(player, spell, cost));
				return;
			}
			this.hitMonster(damage);
			events.push(event("spell_cast", { spellId: spell.id, damage, crit, element: spell.element, mana: cost }));
			this.leech(damage, sheet, events);
			const bonusEffect = spell.level3Bonus;
			if (level.level === 3 && bonusEffect.status !== null && this.rng.chance(bonusEffect.chance)) {
				const perTurn = Math.max(1, pct(damage, this.data.balance.spellStatusDamagePct));
				this.applyStatus("monster", bonusEffect.status, perTurn, events);
			}
		} else {
			const healed = Math.min(amount, sheet.maxHp - player.hp);
			player.hp += healed;
			events.push(event("spell_healed", { spellId: spell.id, amount: healed, mana: cost }));
			if (level.level === 3 && spell.level3Bonus.cleanse) {
				for (const status of [...player.statuses]) {
					player.statuses.splice(player.statuses.indexOf(status), 1);
					events.push(event("status_expired", { target: "player", status: status.statusId }));
				}
			}
		}
		events.push(...this.#progression.afterCast(player, spell, cost));
	}

	private drink(potionId: string, events: Event[]): void {
		const player = this.player;
		const sheet = this.sheet();
		const potion = this.data.potion(potionId);
		player.potions.set(potionId, player.potionCount(potionId) - 1);
		const amount = this.rng.roll(potion.min, potion.max);
		let restored: number;
		if (potion.resource === "hp") {
			restored = Math.min(amount, sheet.maxHp - player.hp);
			player.hp += restored;
		} else {
			restored = Math.min(amount, sheet.maxMp - player.mp);
			player.mp += restored;
		}
		events.push(event("potion_used", { potionId, amount: restored, resource: potion.resource }));
	}

	private rollCrit(damage: number, sheet: CharacterSheet): [number, boolean] {
		if (this.rng.chance(sheet.critChance)) {
			return [pct(damage, this.data.balance.critMultiplierPct + sheet.critDamage), true];
		}
		return [damage, false];
	}

	private resisted(damage: number, element: Element): number {
		const resistance = this.data.creature(this.monster.creatureId).resistance(element);
		if (resistance === 0) return 0;
		return Math.max(1, pct(damage, resistance));
	}

	private hitMonster(damage: number): void {
		this.monster.hp = Math.max(0, this.monster.hp - damage);
	}

	private leech(damage: number, sheet: CharacterSheet, events: Event[]): void {
		const player = this.player;
		const hpGain = Math.min(pct(damage, sheet.lifeLeech), sheet.maxHp - player.hp);
		const mpGain = Math.min(pct(damage, sheet.manaLeech), sheet.maxMp - player.mp);
		if (hpGain <= 0 && mpGain <= 0) return;
		player.hp += Math.max(0, hpGain);
		player.mp += Math.max(0, mpGain);
		events.push(event("leeched", { hp: Math.max(0, hpGain), mp: Math.max(0, mpGain) }));
	}

	// ── step 3: monster phase ─────────────────────────────────────────────────

	private monsterPhase(events: Event[]): void {
		const monster = this.monster;
		this.tick("monster", monster.statuses, events);
		if (monster.hp <= 0) return;
		if (Battle.consumeStun(monster.statuses)) {
			monster.stunCooldown = STUN_COOLDOWN_TURNS;
			events.push(event("monster_stunned"));
			return;
		}
		// A healing monster does nothing else this turn; a boss does not advance its pattern.
		if (monster.hp < monster.maxHp && this.rng.chance(this.enemyClass().heal)) {
			const healed = Math.min(pct(monster.maxHp, this.data.balance.monsterHealPct), monster.maxHp - monster.hp);
			monster.hp += healed;
			events.push(event("monster_healed", { amount: healed }));
			return;
		}

		if (monster.isBoss) {
			const every = this.data.balance.bossTelegraphEvery;
			const position = monster.bossActions % (every + 1);
			monster.bossActions += 1;
			const chargeId = this.data.creature(monster.creatureId).chargeAttack;
			if (chargeId !== null && position === every - 1) {
				const charge = monster.attack(chargeId);
				events.push(event("boss_telegraph", { attackId: charge.id, element: charge.element }));
				return;
			}
			if (chargeId !== null && position === every) {
				this.resolveMonsterAttack(monster.attack(chargeId), true, events);
				return;
			}
		}

		const index = this.rng.weighted(monster.attacks.map((attack) => attack.weight));
		const attack = monster.attacks[index];
		if (attack === undefined) throw new Error("attack index out of range");
		this.resolveMonsterAttack(attack, false, events);
	}

	private resolveMonsterAttack(attack: MonsterAttack, charged: boolean, events: Event[]): void {
		const player = this.player;
		const sheet = this.sheet();
		const balance = this.data.balance;
		if (this.rng.chance(sheet.dodge)) {
			events.push(event("attack_dodged", { attackId: attack.id }));
			return;
		}
		let damage = this.rng.roll(attack.min, attack.max);
		if (charged) damage = pct(damage, balance.bossChargeDamagePct);
		if (attack.element === "physical" && this.rng.chance(sheet.parry)) {
			const reflected = Math.max(1, pct(damage, balance.parryReflectPct));
			this.hitMonster(reflected);
			events.push(event("attack_parried", { attackId: attack.id, reflected }));
			return;
		}
		const crit = this.rng.chance(this.enemyClass().crit);
		if (crit) damage = pct(damage, balance.critMultiplierPct);
		if (attack.element === "physical") damage = armorMitigation(damage, sheet.armor);
		damage = pct(damage, 100 - protection(sheet, attack.element));
		if (player.defending) damage = pct(damage, balance.defendDamagePct);
		damage = Math.max(1, damage);
		player.hp = Math.max(0, player.hp - damage);
		events.push(event("monster_attacked", { attackId: attack.id, damage, element: attack.element, charged, crit }));
		if (attack.status !== null && this.rng.chance(attack.status.chance)) {
			const perTurn = Math.max(1, pct(damage, attack.status.damagePct));
			this.applyStatus("player", attack.status.status, perTurn, events);
		}
	}

	// ── step 5: end of turn ───────────────────────────────────────────────────

	/** Returns true when the player died from status ticks. */
	private endOfTurn(events: Event[]): boolean {
		const player = this.player;
		this.tick("player", player.statuses, events);
		if (player.hp <= 0) return true;
		const sheet = this.sheet();
		const hpGain = Math.max(0, Math.min(sheet.hpRegen, sheet.maxHp - player.hp));
		const mpGain = Math.max(0, Math.min(sheet.mpRegen, sheet.maxMp - player.mp));
		player.hp += hpGain;
		player.mp += mpGain;
		if (hpGain > 0 || mpGain > 0) events.push(event("regenerated", { hp: hpGain, mp: mpGain }));
		player.defending = false;
		player.stunCooldown = Math.max(0, player.stunCooldown - 1);
		this.monster.stunCooldown = Math.max(0, this.monster.stunCooldown - 1);
		this.state.turn += 1;
		return false;
	}

	// ── statuses ──────────────────────────────────────────────────────────────

	private applyStatus(target: Target, statusId: string, perTurn: number, events: Event[]): void {
		const definition = this.data.status(statusId);
		let statuses: ActiveStatus[];
		let cooldown: number;
		if (target === "player") {
			statuses = this.player.statuses;
			cooldown = this.player.stunCooldown;
		} else {
			statuses = this.monster.statuses;
			cooldown = this.monster.stunCooldown;
			if (this.data.creature(this.monster.creatureId).resistance(definition.element) === 0) return;
		}

		if (definition.kind === "stun") {
			if (cooldown > 0 || statuses.some((s) => s.statusId === STUN)) return;
			statuses.push({ statusId: STUN, turns: definition.turns, perTurn: 0 });
			events.push(event("status_applied", { target, status: STUN, turns: definition.turns, perTurn: 0 }));
			return;
		}

		let existing = statuses.find((s) => s.statusId === statusId);
		if (existing === undefined) {
			existing = { statusId, turns: definition.turns, perTurn };
			statuses.push(existing);
		} else {
			existing.turns = definition.turns;
			existing.perTurn = Math.max(existing.perTurn, perTurn);
		}
		events.push(
			event("status_applied", { target, status: statusId, turns: existing.turns, perTurn: existing.perTurn }),
		);
	}

	private tick(target: Target, statuses: ActiveStatus[], events: Event[]): void {
		for (const status of [...statuses]) {
			const definition = this.data.status(status.statusId);
			if (definition.kind !== "dot") continue;
			const damage = this.statusDamage(target, status.perTurn, definition.element);
			if (target === "player") this.player.hp = Math.max(0, this.player.hp - damage);
			else this.hitMonster(damage);
			events.push(event("status_ticked", { target, status: status.statusId, damage }));
			status.turns -= 1;
			if (status.turns <= 0) {
				statuses.splice(statuses.indexOf(status), 1);
				events.push(event("status_expired", { target, status: status.statusId }));
			}
		}
	}

	private statusDamage(target: Target, perTurn: number, element: Element): number {
		if (target === "player") return Math.max(1, pct(perTurn, 100 - protection(this.sheet(), element)));
		const resistance = this.data.creature(this.monster.creatureId).resistance(element);
		return resistance === 0 ? 0 : Math.max(1, pct(perTurn, resistance));
	}

	private static consumeStun(statuses: ActiveStatus[]): boolean {
		const index = statuses.findIndex((status) => status.statusId === STUN);
		if (index < 0) return false;
		statuses.splice(index, 1);
		return true;
	}
}
