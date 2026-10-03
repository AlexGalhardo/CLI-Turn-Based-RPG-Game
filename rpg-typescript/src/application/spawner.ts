/** Monster spawning for a round: tier, cycle, position, enemy class and difficulty scaling (docs/game-design.md §3). */
import type { DifficultyDef, GameData, MonsterDef } from "../domain/definitions";
import { MonsterInstance } from "../domain/entities";
import type { EnemyClass } from "../domain/enums";
import { pct, type RoundInfo, roundInfo, scaleReward, scaleStat, scaling } from "../domain/formulas";
import type { Rng } from "../domain/rng";

export function spawnMonster(
	data: GameData,
	rng: Rng,
	roundNumber: number,
	difficulty: DifficultyDef,
): [MonsterInstance, RoundInfo] {
	const balance = data.balance;
	const info = roundInfo(roundNumber, balance, data.tierCount);
	let creature: MonsterDef;
	let enemyClass: EnemyClass;
	if (info.isBoss) {
		creature = data.bossOfTier(info.tier);
		enemyClass = "boss";
	} else {
		creature = rng.pick(data.monstersInTier(info.tier));
		enemyClass = rng.chance(balance.eliteChancePct) ? "elite" : "normal";
	}
	const row = balance.enemyClass(enemyClass);

	const factors = scaling(info, balance, difficulty);
	const stat = (value: number, product: number): number => Math.max(1, pct(scaleStat(value, product), row.statPct));
	const reward = (value: number, product: number): number => pct(scaleReward(value, product), row.rewardPct);

	const hp = stat(creature.hp, factors.hpPctProduct);
	const attacks = creature.attacks.map((attack) => ({
		...attack,
		min: stat(attack.min, factors.damagePctProduct),
		max: stat(attack.max, factors.damagePctProduct),
	}));
	const monster = new MonsterInstance(
		creature.id,
		creature.isBoss,
		enemyClass,
		hp,
		hp,
		reward(creature.xp, factors.rewardXpPctProduct),
		reward(creature.goldMin, factors.rewardGoldPctProduct),
		reward(creature.goldMax, factors.rewardGoldPctProduct),
		attacks,
	);
	return [monster, info];
}
