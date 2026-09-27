package application

import "github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"

// SpawnMonster creates the monster of a round: tier, cycle, position and difficulty scaling (docs/game-design.md §3).
func SpawnMonster(data *domain.GameData, rng *domain.Rng, round int, difficulty domain.DifficultyDef) (*domain.MonsterInstance, domain.RoundInfo) {
	info := domain.RoundInfoFor(round, &data.Balance, data.TierCount())

	var creature *domain.MonsterDef
	if info.IsBoss {
		creature = data.BossOfTier(info.Tier)
	} else {
		creature = domain.Pick(rng, data.MonstersInTier(info.Tier))
	}

	factors := domain.ScalingFor(info, &data.Balance, difficulty)
	hp := max(1, domain.ScaleStat(creature.HP, factors.HPPctProduct))

	attacks := make([]domain.MonsterAttack, len(creature.Attacks))
	for i, attack := range creature.Attacks {
		attack.Min = max(1, domain.ScaleStat(attack.Min, factors.DamagePctProduct))
		attack.Max = max(1, domain.ScaleStat(attack.Max, factors.DamagePctProduct))
		attacks[i] = attack
	}

	return &domain.MonsterInstance{
		CreatureID: creature.ID,
		IsBoss:     creature.IsBoss,
		HP:         hp,
		MaxHP:      hp,
		XP:         domain.ScaleReward(creature.XP, factors.RewardXPPctProduct),
		GoldMin:    domain.ScaleReward(creature.GoldMin, factors.RewardGoldPctProduct),
		GoldMax:    domain.ScaleReward(creature.GoldMax, factors.RewardGoldPctProduct),
		Attacks:    attacks,
		Statuses:   []domain.ActiveStatus{},
	}, info
}
