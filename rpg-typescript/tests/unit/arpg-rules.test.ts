/**
 * M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects,
 * class drop tables, potion drops, spell level effects and the victory phase. Port of tests/unit/test_arpg_rules.py.
 */
import { describe, expect, test } from "bun:test";
import {
	Attack,
	BuyPotion,
	Cast,
	ContinueRun,
	Defend,
	EndRun,
	Equip,
	NextFight,
	SellItem,
} from "../../src/application/commands";
import type { GameEngine } from "../../src/application/engine";
import type { Event } from "../../src/application/events";
import { spawnMonster } from "../../src/application/spawner";
import { buildSheet } from "../../src/domain/character";
import type { GameData, ItemDef } from "../../src/domain/definitions";
import type { MonsterInstance } from "../../src/domain/entities";
import type { Element, Phase } from "../../src/domain/enums";
import { pct } from "../../src/domain/formulas";
import { Rng } from "../../src/domain/rng";
import { calm, DATA, newEngine, withEnemyClass } from "../helpers";

const MAX_TURNS = 300;

const types = (events: readonly Event[]): string[] => events.map((e) => e.type);

const engineWith = (data: GameData, vocation = "warrior"): GameEngine => newEngine(vocation, "normal", 42, data);

const withEliteChance = (data: GameData, eliteChancePct: number): GameData =>
	data.with({ balance: data.balance.with({ eliteChancePct }) });

function fight(engine: GameEngine, damage = 1, element: Element = "physical"): MonsterInstance {
	engine.step(NextFight());
	const monster = engine.state.monster;
	if (monster === null) throw new Error("no monster");
	monster.attacks = [{ id: "test_hit", element, min: damage, max: damage, weight: 1, status: null }];
	monster.hp = 10_000;
	monster.maxHp = 10_000;
	return monster;
}

/** Finishes the current fight with melee hits (the monster is left with 1 HP before each hit). */
function kill(engine: GameEngine): Event[] {
	for (let i = 0; i < MAX_TURNS; i++) {
		const monster = engine.state.monster;
		if (monster === null) break;
		monster.hp = 1;
		engine.state.player.hp = 1_000_000;
		const events = engine.step(Attack());
		if (engine.state.phase !== "battle") return events;
	}
	throw new Error("the fight did not end");
}

/** Reads the phase through a function so the type checker doesn't narrow it between steps. */
const phaseOf = (engine: GameEngine): Phase => engine.state.phase;

function physicalResistant100(data: GameData): string {
	const monster = data.monsters.find((m) => m.resistance("physical") === 100);
	if (monster === undefined) throw new Error("no monster with 100% physical resistance");
	return monster.id;
}

function intOf(evt: Event | undefined, name: string): number {
	const value = evt?.[name];
	if (typeof value !== "number") throw new Error(`missing ${name}`);
	return value;
}

describe("spawn", () => {
	test("elite spawn scales stats and rewards", () => {
		const normalData = calm(DATA);
		const eliteData = withEliteChance(normalData, 100);
		const difficulty = DATA.balance.difficulty("normal");
		const [normal] = spawnMonster(normalData, new Rng(5), 3, difficulty);
		const [elite] = spawnMonster(eliteData, new Rng(5), 3, difficulty);
		const row = DATA.balance.enemyClass("elite");
		expect([normal.enemyClass, elite.enemyClass]).toEqual(["normal", "elite"]);
		expect(elite.creatureId).toBe(normal.creatureId);
		expect(elite.maxHp).toBe(pct(normal.maxHp, row.statPct));
		expect(elite.attacks[0]?.max).toBe(pct(normal.attacks[0]?.max ?? 0, row.statPct));
		expect(elite.xp).toBe(pct(normal.xp, row.rewardPct));
		expect(elite.goldMax).toBe(pct(normal.goldMax, row.rewardPct));
	});

	test("elite roll follows the monster pick", () => {
		const difficulty = DATA.balance.difficulty("normal");
		const rng = new Rng(77);
		const classes = Array.from(
			{ length: 300 },
			(_, i) => spawnMonster(DATA, rng, 1 + (i % 9), difficulty)[0].enemyClass,
		);
		expect(new Set(classes)).toEqual(new Set(["normal", "elite"]));
		const elites = classes.filter((c) => c === "elite").length;
		expect(elites).toBeGreaterThanOrEqual(30);
		expect(elites).toBeLessThanOrEqual(90);
		expect(spawnMonster(DATA, new Rng(1), 10, difficulty)[0].enemyClass).toBe("boss");
	});

	test("round_started reports the enemy class", () => {
		const engine = engineWith(withEliteChance(DATA, 100));
		const started = engine.step(NextFight())[0];
		expect(started?.enemyClass).toBe("elite");
		expect(started?.isBoss).toBe(false);
	});
});

describe("monster dodge, parry, heal and crit", () => {
	test("monster dodge stops melee and spells", () => {
		const engine = engineWith(withEnemyClass(calm(DATA), "normal", { dodge: 100 }));
		const monster = fight(engine);
		let events = engine.step(Attack());
		expect(events[0]).toEqual({ type: "monster_dodged" });
		expect(types(events)).not.toContain("player_attacked");
		expect(monster.hp).toBe(monster.maxHp);
		const player = engine.state.player;
		player.mp = 1000;
		events = engine.step(Cast("brutal_strike"));
		expect(events[0]).toEqual({ type: "monster_dodged" });
		expect(player.mp).toBeLessThan(1000);
		expect(player.spellUses.get("brutal_strike")).toBe(1);
	});

	test("monster parry reflects physical hits", () => {
		const engine = engineWith(withEnemyClass(calm(DATA), "normal", { parry: 100 }));
		const monster = fight(engine);
		const player = engine.state.player;
		let events = engine.step(Defend());
		expect(types(events)).not.toContain("monster_parried");
		const hp = player.hp;
		events = engine.step(Attack());
		const parried = events[0];
		expect(parried?.type).toBe("monster_parried");
		const reflected = intOf(parried, "reflected");
		expect(reflected).toBeGreaterThanOrEqual(1);
		expect(types(events)).not.toContain("leeched");
		expect(monster.hp).toBe(monster.maxHp);
		const hits = events
			.filter((e) => e.type === "monster_attacked")
			.reduce((sum, e) => sum + intOf(e, "damage"), 0);
		const regen = events.filter((e) => e.type === "regenerated").reduce((sum, e) => sum + intOf(e, "hp"), 0);
		expect(player.hp).toBe(hp - reflected - hits + regen);
	});

	test("monster parry reflect can kill the player", () => {
		const engine = engineWith(withEnemyClass(calm(DATA), "normal", { parry: 100 }));
		const monster = fight(engine);
		engine.state.player.hp = 1;
		const events = engine.step(Attack());
		expect(types(events)).toEqual(["monster_parried", "player_died"]);
		expect(engine.state.phase).toBe("game_over");
		expect(monster.hp).toBe(monster.maxHp);
	});

	test("monster parry ignores non-physical spells", () => {
		const engine = engineWith(withEnemyClass(calm(DATA), "normal", { parry: 100 }), "mage");
		fight(engine);
		engine.state.player.mp = 1000;
		const events = engine.step(Cast("flame_strike"));
		expect(types(events)).not.toContain("monster_parried");
		expect(types(events)).toContain("spell_cast");
	});

	test("monster heals instead of attacking", () => {
		const engine = engineWith(withEnemyClass(calm(DATA), "normal", { heal: 100 }));
		const monster = fight(engine);
		let events = engine.step(Defend());
		expect(types(events)).not.toContain("monster_healed");
		expect(types(events)).toContain("monster_attacked");
		monster.hp = monster.maxHp - 5;
		events = engine.step(Defend());
		expect(events).toContainEqual({ type: "monster_healed", amount: 5 });
		expect(types(events)).not.toContain("monster_attacked");
		monster.hp = 100;
		events = engine.step(Defend());
		expect(events).toContainEqual({
			type: "monster_healed",
			amount: pct(monster.maxHp, DATA.balance.monsterHealPct),
		});
	});

	test("a healing boss does not advance its pattern", () => {
		const engine = engineWith(withEnemyClass(calm(DATA), "boss", { heal: 100 }));
		engine.state.round = 9;
		engine.step(NextFight());
		const boss = engine.state.monster;
		if (boss === null) throw new Error("no boss");
		boss.hp = boss.maxHp - 1;
		engine.state.player.hp = 1_000_000;
		engine.step(Defend());
		expect(boss.bossActions).toBe(0);
	});

	for (const [crit, expected] of [
		[0, 100],
		[100, 150],
	] as const) {
		test(`monster crit ${crit}% multiplies raw damage`, () => {
			const engine = engineWith(withEnemyClass(calm(DATA), "normal", { crit }));
			fight(engine, 100, "fire");
			engine.state.player.hp = 1000;
			const hit = engine.step(Attack()).find((e) => e.type === "monster_attacked");
			expect(hit?.crit).toBe(crit === 100);
			expect(hit?.damage).toBe(expected);
		});
	}

	test("player parry reflects damage to the monster", () => {
		const shield: ItemDef = {
			id: "test_parry_shield",
			name: "Test Shield",
			slot: "shield",
			type: "shield",
			tier: 0,
			element: null,
			stats: [["parry", 100]],
			value: 10,
		};
		const base = calm(DATA);
		const caps = base.balance.caps;
		const data = base.with({
			items: [...DATA.items, shield],
			balance: base.balance.with({ caps: { ...caps, dodge: 0, parry: 100, leech: 25, protection: 75 } }),
		});
		const engine = engineWith(data);
		engine.state.player.equipment.set("shield", {
			uid: 90,
			itemId: "test_parry_shield",
			rarity: "common",
			tier: 0,
			affixes: [],
		});
		expect(buildSheet(engine.state.player, data).parry).toBe(100);
		const monster = fight(engine, 50);
		let events = engine.step(Defend());
		expect(events).toContainEqual({ type: "attack_parried", attackId: "test_hit", reflected: 10 });
		expect(monster.hp).toBe(monster.maxHp - 10);
		expect(engine.state.stats.parries).toBe(1);
		monster.hp = 5;
		events = engine.step(Defend());
		expect(types(events)).toContain("monster_killed");
		expect(engine.state.phase).toBe("merchant");
	});
});

describe("victory, drops and potions", () => {
	test("normal drop table", () => {
		let engine = engineWith(withEnemyClass(calm(DATA), "normal", { dropChancePct: 100 }));
		fight(engine);
		const events = kill(engine);
		const drops = events.filter((e) => e.type === "item_dropped");
		expect(drops).toHaveLength(1);
		expect(["common", "rare"]).toContain(String(drops[0]?.rarity));
		expect(types(events)).not.toContain("potion_dropped");
		engine = engineWith(withEnemyClass(calm(DATA), "normal", { dropChancePct: 0 }));
		fight(engine);
		expect(types(kill(engine))).not.toContain("item_dropped");
	});

	test("an elite drops a good item and maybe a potion", () => {
		const data = withEliteChance(withEnemyClass(calm(DATA), "elite", { potionDropPct: 100 }), 100);
		const engine = engineWith(data);
		fight(engine);
		const before = new Map(engine.state.player.potions);
		const events = kill(engine);
		const drops = events.filter((e) => e.type === "item_dropped");
		expect(drops).toHaveLength(1);
		expect(["rare", "legendary"]).toContain(String(drops[0]?.rarity));
		const potion = events.find((e) => e.type === "potion_dropped");
		const potionId = String(potion?.potionId);
		expect(DATA.potion(potionId).unlockRound).toBeLessThanOrEqual(1);
		expect(engine.state.player.potions.get(potionId)).toBe((before.get(potionId) ?? 0) + 1);
		expect(engine.state.stats.potionsDropped.get(potionId)).toBe(1);
		expect(engine.state.stats.elitesKilled).toBe(1);
		expect(events.find((e) => e.type === "monster_killed")?.enemyClass).toBe("elite");
	});

	test("a potion drop without unlocked potions consumes nothing", () => {
		const locked = DATA.potions.map((p) => ({ ...p, unlockRound: 99 }));
		const data = withEnemyClass(calm(DATA), "normal", { dropChancePct: 0, potionDropPct: 100 });
		const engine = engineWith(data.with({ potions: locked }));
		fight(engine);
		expect(types(kill(engine))).not.toContain("potion_dropped");
	});

	test("a boss drops several top items", () => {
		const engine = engineWith(calm(DATA));
		engine.state.round = 9;
		fight(engine);
		const drops = kill(engine).filter((e) => e.type === "item_dropped");
		expect(drops).toHaveLength(DATA.balance.enemyClass("boss").drops);
		for (const drop of drops) expect(["legendary", "mythic"]).toContain(String(drop.rarity));
	});

	test("a full bag auto-sells drops", () => {
		const engine = engineWith(withEnemyClass(calm(DATA), "normal", { dropChancePct: 100 }));
		const player = engine.state.player;
		for (let i = 0; i < DATA.balance.bagCapacity; i++) {
			player.bag.push({ uid: 500 + i, itemId: "sword", rarity: "common", tier: 0, affixes: [] });
		}
		fight(engine);
		expect(types(kill(engine))).toContain("item_auto_sold");
		expect(player.bag).toHaveLength(DATA.balance.bagCapacity);
	});

	function finalVictory(engine: GameEngine): Event[] {
		engine.state.round = engine.data.balance.finalRound - 1;
		fight(engine);
		return kill(engine);
	}

	test("beating the final boss enters the victory phase", () => {
		const engine = engineWith(calm(DATA));
		const events = finalVictory(engine);
		expect(events.at(-1)).toEqual({ type: "run_won", round: DATA.balance.finalRound });
		expect(types(events)).not.toContain("merchant_entered");
		expect(engine.state.phase).toBe("victory");
		expect(engine.state.won).toBe(true);
		const rngState = engine.rngState;
		const stock = [...engine.state.merchantStock];
		for (const command of [Attack(), Defend(), NextFight(), BuyPotion("health_potion", 1), Equip(1), SellItem(1)]) {
			expect(engine.step(command)).toEqual([{ type: "error", code: "invalid_phase" }]);
		}
		expect(engine.rngState).toBe(rngState);
		expect(engine.state.merchantStock).toEqual(stock);
		expect(engine.step(EndRun())).toEqual([{ type: "run_ended", won: true }]);
		expect(phaseOf(engine)).toBe("game_over");
		expect(engine.state.deathCause).toBeNull();
		expect(engine.step(ContinueRun())).toEqual([{ type: "error", code: "invalid_phase" }]);
	});

	test("continue_run enters the merchant", () => {
		const engine = engineWith(calm(DATA));
		finalVictory(engine);
		expect(engine.step(ContinueRun())).toEqual([{ type: "merchant_entered", round: DATA.balance.finalRound }]);
		expect(engine.state.phase).toBe("merchant");
		expect(engine.state.merchantStock).toHaveLength(DATA.balance.merchantStockSize);
		engine.step(NextFight());
		expect(engine.state.round).toBe(DATA.balance.finalRound + 1);
		expect(engine.state.won).toBe(true);
	});

	test("victory commands are rejected outside the victory phase", () => {
		const engine = newEngine();
		expect(engine.step(EndRun())).toEqual([{ type: "error", code: "invalid_phase" }]);
		expect(engine.step(ContinueRun())).toEqual([{ type: "error", code: "invalid_phase" }]);
	});
});

describe("spell levels", () => {
	for (const [uses, effect] of [
		[0, 100],
		[20, 150],
		[50, 200],
	] as const) {
		test(`spell level effect scales damage (${uses} uses → ${effect}%)`, () => {
			const engine = engineWith(calm(DATA));
			const monster = fight(engine);
			monster.creatureId = physicalResistant100(DATA);
			const player = engine.state.player;
			player.spellUses.set("brutal_strike", uses);
			player.mp = 1000;
			const rng = new Rng(engine.rngState);
			const spell = DATA.spell("brutal_strike");
			const bonus = player.level * spell.perLevel + player.magicLevel * spell.perMagicLevel;
			const base = rng.roll(spell.min + bonus, spell.max + bonus);
			const cast = engine.step(Cast("brutal_strike")).find((e) => e.type === "spell_cast");
			expect(cast?.damage).toBe(Math.max(1, pct(base, effect)));
		});
	}

	test("spell level effect scales healing", () => {
		const engine = engineWith(calm(DATA));
		fight(engine);
		const player = engine.state.player;
		player.spellUses.set("wound_cleansing", 20);
		player.mp = 1000;
		player.hp = 1;
		const rng = new Rng(engine.rngState);
		const spell = DATA.spell("wound_cleansing");
		const bonus = player.level * spell.perLevel + player.magicLevel * spell.perMagicLevel;
		const expected = pct(rng.roll(spell.min + bonus, spell.max + bonus), 150);
		const room = buildSheet(player, DATA).maxHp - player.hp;
		const healed = engine.step(Cast("wound_cleansing")).find((e) => e.type === "spell_healed");
		expect(healed?.amount).toBe(Math.min(expected, room));
	});
});
