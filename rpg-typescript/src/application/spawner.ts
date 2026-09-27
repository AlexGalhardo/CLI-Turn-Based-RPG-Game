/** Monster spawning for a round: tier, cycle, position and difficulty scaling (docs/game-design.md §3). */
import type { DifficultyDef, GameData } from "../domain/definitions";
import { MonsterInstance } from "../domain/entities";
import { type RoundInfo, roundInfo, scaleReward, scaleStat, scaling } from "../domain/formulas";
import type { Rng } from "../domain/rng";

export function spawnMonster(
	data: GameData,
	rng: Rng,
	roundNumber: number,
	difficulty: DifficultyDef,
): [MonsterInstance, RoundInfo] {
	const info = roundInfo(roundNumber, data.balance, data.tierCount);
	const creature = info.isBoss ? data.bossOfTier(info.tier) : rng.pick(data.monstersInTier(info.tier));
	const factors = scaling(info, data.balance, difficulty);
	const hp = Math.max(1, scaleStat(creature.hp, factors.hpPctProduct));
	const attacks = creature.attacks.map((attack) => ({
		...attack,
		min: Math.max(1, scaleStat(attack.min, factors.damagePctProduct)),
		max: Math.max(1, scaleStat(attack.max, factors.damagePctProduct)),
	}));
	const monster = new MonsterInstance(
		creature.id,
		creature.isBoss,
		hp,
		hp,
		scaleReward(creature.xp, factors.rewardXpPctProduct),
		scaleReward(creature.goldMin, factors.rewardGoldPctProduct),
		scaleReward(creature.goldMax, factors.rewardGoldPctProduct),
		attacks,
	);
	return [monster, info];
}
