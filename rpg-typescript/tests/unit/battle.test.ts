import { describe, expect, test } from "bun:test";
import {
	Attack,
	BuyPotion,
	Cast,
	type Command,
	commandFromJson,
	commandToJson,
	Defend,
	NextFight,
	UsePotion,
} from "../../src/application/commands";
import type { GameEngine } from "../../src/application/engine";
import type { Event } from "../../src/application/events";
import { buildSheet } from "../../src/domain/character";
import type { StatusOnHit } from "../../src/domain/definitions";
import type { MonsterInstance } from "../../src/domain/entities";
import type { Element } from "../../src/domain/enums";
import { DATA, newEngine, withTestItems } from "../helpers";

const types = (events: readonly Event[]): string[] => events.map((e) => e.type);

function fight(engine: GameEngine): MonsterInstance {
	engine.step(NextFight());
	const monster = engine.state.monster;
	if (monster === null) throw new Error("no monster");
	return monster;
}

function fixedAttack(
	monster: MonsterInstance,
	damage: number,
	element: Element = "physical",
	status: StatusOnHit | null = null,
): void {
	monster.attacks = [{ id: "test_hit", element, min: damage, max: damage, weight: 1, status }];
	monster.hp = 10_000;
	monster.maxHp = 10_000;
}

describe("battle", () => {
	test("melee damage stays within the sheet range", () => {
		const engine = newEngine();
		const monster = fight(engine);
		const sheet = buildSheet(engine.state.player, DATA);
		const hit = engine.step(Attack()).find((e) => e.type === "player_attacked");
		const resistance = DATA.creature(monster.creatureId).resistance("physical");
		expect(hit?.crit).toBe(false);
		expect(Number(hit?.damage)).toBeGreaterThanOrEqual(
			Math.max(1, Math.floor((sheet.meleeMin * resistance) / 100)),
		);
		expect(Number(hit?.damage)).toBeLessThanOrEqual(Math.max(1, Math.floor((sheet.meleeMax * resistance) / 100)));
	});

	test("invalid battle commands don't consume randomness", () => {
		const engine = newEngine();
		expect(engine.step(Attack())).toEqual([{ type: "error", code: "invalid_phase" }]);
		fight(engine);
		const rngState = engine.rngState;
		expect(engine.step(Cast("flame_strike"))).toEqual([{ type: "error", code: "unknown_spell" }]);
		engine.state.player.mp = 0;
		expect(engine.step(Cast("brutal_strike"))).toEqual([{ type: "error", code: "not_enough_mana" }]);
		engine.state.player.potions.clear();
		expect(engine.step(UsePotion("health_potion"))).toEqual([{ type: "error", code: "no_potion" }]);
		expect(engine.step(UsePotion("elixir"))).toEqual([{ type: "error", code: "unknown_potion" }]);
		expect(engine.step(NextFight())).toEqual([{ type: "error", code: "invalid_phase" }]);
		expect(engine.step(BuyPotion("health_potion", 1))).toEqual([{ type: "error", code: "invalid_phase" }]);
		expect(engine.rngState).toBe(rngState);
	});

	test("defend halves damage; big hits kill", () => {
		const engine = newEngine();
		fixedAttack(fight(engine), 100);
		engine.state.player.hp = 150;
		expect(engine.step(Defend()).find((e) => e.type === "monster_attacked")?.damage).toBe(50);
		expect(engine.state.player.defending).toBe(false);

		const other = newEngine();
		const monster = fight(other);
		fixedAttack(monster, 1000);
		const events = other.step(Attack());
		expect(events[events.length - 1]).toEqual({ type: "player_died", monsterId: monster.creatureId, round: 1 });
		expect(other.state.phase).toBe("game_over");
		expect(other.step(Attack())).toEqual([{ type: "error", code: "invalid_phase" }]);
	});

	test("statuses apply, refresh, tick and expire", () => {
		const engine = newEngine();
		fixedAttack(fight(engine), 10, "fire", { status: "burn", chance: 100, damagePct: 50 });
		const events = engine.step(Defend());
		expect(events.find((e) => e.type === "status_applied")).toEqual({
			type: "status_applied",
			target: "player",
			status: "burn",
			turns: 3,
			perTurn: 2,
		});
		expect(engine.state.player.statuses).toEqual([{ statusId: "burn", turns: 2, perTurn: 2 }]);

		const refresh = newEngine();
		fixedAttack(fight(refresh), 10, "fire", { status: "burn", chance: 100, damagePct: 50 });
		refresh.state.player.statuses.push({ statusId: "burn", turns: 1, perTurn: 9 });
		refresh.step(Defend());
		expect(refresh.state.player.statuses).toEqual([{ statusId: "burn", turns: 2, perTurn: 9 }]);

		const expire = newEngine();
		fixedAttack(fight(expire), 1);
		expire.state.player.statuses.push({ statusId: "bleed", turns: 1, perTurn: 1 });
		expect(types(expire.step(Defend()))).toContain("status_expired");
	});

	test("stuns skip turns and respect the cooldown", () => {
		const engine = newEngine();
		fixedAttack(fight(engine), 1, "physical", { status: "stun", chance: 100, damagePct: 0 });
		const first = types(engine.step(Defend()));
		expect(first.filter((t) => t === "monster_attacked")).toHaveLength(2);
		expect(first).toContain("player_stunned");
		expect(engine.state.player.stunCooldown).toBe(1);
		expect(types(engine.step(Defend()))).not.toContain("status_applied");

		const other = newEngine();
		const monster = fight(other);
		fixedAttack(monster, 5);
		monster.statuses.push({ statusId: "stun", turns: 1, perTurn: 0 });
		const events = types(other.step(Defend()));
		expect(events).toContain("monster_stunned");
		expect(events).not.toContain("monster_attacked");
	});

	test("monster dies from its own status tick", () => {
		const engine = newEngine();
		const monster = fight(engine);
		monster.hp = 1;
		monster.statuses.push({ statusId: "bleed", turns: 3, perTurn: 5 });
		expect(types(engine.step(Defend()))).toContain("monster_killed");
		expect(engine.state.phase).toBe("merchant");
	});

	test("bosses telegraph then charge", () => {
		const engine = newEngine();
		engine.state.round = 9;
		const boss = fight(engine);
		expect(boss.isBoss).toBe(true);
		boss.hp = 1_000_000;
		boss.maxHp = 1_000_000;
		boss.attacks = boss.attacks.map((attack) => ({ ...attack, status: null }));
		engine.state.player.hp = 1_000_000;
		const sequence = [0, 1, 2, 3].map(() => {
			const e = engine.step(Defend()).find((x) => x.type === "boss_telegraph" || x.type === "monster_attacked");
			return e?.type === "boss_telegraph" ? "telegraph" : e?.charged === true ? "charged" : "normal";
		});
		expect(sequence).toEqual(["normal", "normal", "telegraph", "charged"]);
	});

	test("heals are capped and level 3 cleanses; spell and magic levels grow", () => {
		const engine = newEngine();
		fixedAttack(fight(engine), 1);
		const player = engine.state.player;
		player.spellUses.set("wound_cleansing", 50);
		player.statuses.push({ statusId: "poison", turns: 5, perTurn: 1 });
		player.hp = buildSheet(player, DATA).maxHp - 3;
		const events = engine.step(Cast("wound_cleansing"));
		expect(events.find((e) => e.type === "spell_healed")).toMatchObject({ amount: 3, mana: 32 });
		expect(events).toContainEqual({ type: "status_expired", target: "player", status: "poison" });

		player.spellUses.set("brutal_strike", 19);
		player.mp = 1000;
		player.manaSpent = DATA.balance.magicLevelBase - 1;
		const cast = engine.step(Cast("brutal_strike"));
		expect(cast).toContainEqual({ type: "spell_level_up", spellId: "brutal_strike", level: 2 });
		expect(cast.some((e) => e.type === "magic_level_up")).toBe(true);
	});

	test("immune monsters take no damage; items add crit and dodge", () => {
		const engine = newEngine("mage");
		const monster = fight(engine);
		fixedAttack(monster, 1);
		monster.creatureId = "fire_elemental";
		engine.state.player.mp = 1000;
		expect(engine.step(Cast("flame_strike")).find((e) => e.type === "spell_cast")?.damage).toBe(0);

		const data = withTestItems(DATA);
		const geared = newEngine("warrior", "normal", 42, data);
		geared.state.player.equipment.set("ring", {
			uid: 99,
			itemId: "test_ring",
			rarity: "common",
			tier: 0,
			affixes: [],
		});
		fixedAttack(fight(geared), 5);
		let crits = 0;
		let dodges = 0;
		for (let i = 0; i < 60; i++) {
			geared.state.player.hp = 100;
			const events = geared.step(Attack());
			crits += events.filter((e) => e.type === "player_attacked" && e.crit === true).length;
			dodges += types(events).filter((t) => t === "attack_dodged").length;
		}
		expect(crits).toBeGreaterThan(0);
		expect(dodges).toBeGreaterThan(0);
	});

	test.each(["warrior", "archer", "mage"])("every %s spell can be cast", (vocation) => {
		const engine = newEngine(vocation);
		fixedAttack(fight(engine), 1);
		for (const spellId of DATA.vocation(vocation).spells) {
			engine.state.player.mp = 10_000;
			engine.state.player.hp = 5;
			const events = engine.step(Cast(spellId));
			expect(
				events.some((e) => (e.type === "spell_cast" || e.type === "spell_healed") && e.spellId === spellId),
			).toBe(true);
		}
	});

	test("potions restore and are consumed", () => {
		const engine = newEngine();
		fixedAttack(fight(engine), 1);
		const player = engine.state.player;
		const before = player.potionCount("mana_potion");
		player.mp = 0;
		expect(engine.step(UsePotion("mana_potion")).find((e) => e.type === "potion_used")?.resource).toBe("mp");
		expect(player.potionCount("mana_potion")).toBe(before - 1);
	});

	test("commands round-trip through JSON", () => {
		const commands: Command[] = [
			Attack(),
			Cast("brutal_strike"),
			UsePotion("health_potion"),
			Defend(),
			NextFight(),
			BuyPotion("mana_potion", 3),
			{ type: "sell_item", uid: 4 },
			{ type: "equip", uid: 5 },
			{ type: "unequip", slot: "ring" },
			{ type: "buy_stock_item", index: 1 },
		];
		for (const command of commands) {
			expect(commandFromJson(JSON.parse(JSON.stringify(commandToJson(command))))).toEqual(command);
		}
		expect(() => commandFromJson({ type: "dance" })).toThrow("unknown command");
	});
});
