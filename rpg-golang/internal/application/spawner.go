package application

import "github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"

// SpawnMonster creates the monster of a round: tier, cycle, position, enemy class and difficulty scaling
// (docs/game-design.md §3).
func SpawnMonster(data *domain.GameData, rng *domain.Rng, round int, difficulty domain.DifficultyDef) (*domain.MonsterInstance, domain.RoundInfo) {
	balance := &data.Balance
	info := domain.RoundInfoFor(round, balance, data.TierCount())

	var (
		creature   *domain.MonsterDef
		enemyClass domain.EnemyClass
	)

	if info.IsBoss {
		creature = data.BossOfTier(info.Tier)
		enemyClass = domain.EnemyBoss
	} else {
		creature = domain.Pick(rng, data.MonstersInTier(info.Tier))
		enemyClass = domain.EnemyNormal

		if rng.Chance(balance.EliteChancePct) {
			enemyClass = domain.EnemyElite
		}
	}

	row := balance.MustEnemyClass(string(enemyClass))
	factors := domain.ScalingFor(info, balance, difficulty)
	stat := func(value, product int) int { return max(1, domain.Pct(domain.ScaleStat(value, product), row.StatPct)) }
	reward := func(value, product int) int { return domain.Pct(domain.ScaleReward(value, product), row.RewardPct) }
	hp := stat(creature.HP, factors.HPPctProduct)

	attacks := make([]domain.MonsterAttack, len(creature.Attacks))
	for i, attack := range creature.Attacks {
		attack.Min = stat(attack.Min, factors.DamagePctProduct)
		attack.Max = stat(attack.Max, factors.DamagePctProduct)
		attacks[i] = attack
	}

	return &domain.MonsterInstance{
		CreatureID: creature.ID,
		IsBoss:     creature.IsBoss,
		EnemyClass: string(enemyClass),
		HP:         hp,
		MaxHP:      hp,
		XP:         reward(creature.XP, factors.RewardXPPctProduct),
		GoldMin:    reward(creature.GoldMin, factors.RewardGoldPctProduct),
		GoldMax:    reward(creature.GoldMax, factors.RewardGoldPctProduct),
		Attacks:    attacks,
		Statuses:   []domain.ActiveStatus{},
	}, info
}
