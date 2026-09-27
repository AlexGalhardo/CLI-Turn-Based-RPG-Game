package domain

// Pct is floor(value * percent / 100) for non-negative operands — the only rounding rule of the engine.
func Pct(value, percent int) int {
	if value < 0 || percent < 0 {
		panic("pct operands must be non-negative")
	}

	return value * percent / 100
}

// Clamp limits value to [minimum, maximum].
func Clamp(value, minimum, maximum int) int {
	return max(minimum, min(maximum, value))
}

// XPForLevel is the total experience needed to reach level (Tibia formula).
func XPForLevel(level int) int {
	if level <= 1 {
		return 0
	}

	return 50 * (level*level*level - 6*level*level + 17*level - 12) / 3
}

// ManaForMagicLevel is the total mana spent (since level 1) needed to advance from magicLevel to the next one.
func ManaForMagicLevel(magicLevel int, balance *Balance) int {
	step := balance.MagicLevelBase

	total := step
	for level := 1; level < magicLevel; level++ {
		step = Pct(step, balance.MagicLevelGrowthPct)
		total += step
	}

	return total
}

// SpellLevelForUses returns the level reached with a number of uses.
func SpellLevelForUses(uses int, levels []SpellLevelDef) SpellLevelDef {
	current := levels[0]
	for _, level := range levels {
		if uses >= level.Uses {
			current = level
		}
	}

	return current
}

// ArmorMitigation reduces physical damage by armor.
func ArmorMitigation(damage, armor int) int {
	return damage * 100 / (100 + armor)
}

// RoundInfo locates a round in the infinite loop.
type RoundInfo struct {
	Round    int
	Tier     int
	Cycle    int
	Position int
	IsBoss   bool
}

// RoundInfoFor computes tier, cycle and position of a round.
func RoundInfoFor(round int, balance *Balance, tierCount int) RoundInfo {
	index := round - 1
	perTier := balance.RoundsPerTier
	position := index % perTier

	return RoundInfo{
		Round:    round,
		Tier:     (index / perTier) % tierCount,
		Cycle:    index / (perTier * tierCount),
		Position: position,
		IsBoss:   position == perTier-1,
	}
}

// Scaling holds the chained percentage products for a round.
type Scaling struct {
	HPPctProduct         int
	DamagePctProduct     int
	RewardXPPctProduct   int
	RewardGoldPctProduct int
}

// ScalingFor combines difficulty, cycle and position.
func ScalingFor(info RoundInfo, balance *Balance, difficulty DifficultyDef) Scaling {
	cyclePct := 100 + info.Cycle*balance.CycleStatPct
	rewardPct := 100 + info.Cycle*balance.CycleRewardPct

	positionPct := 100 + info.Position*balance.PositionPct
	if info.IsBoss {
		positionPct = 100
	}

	return Scaling{
		HPPctProduct:         difficulty.HPPct * cyclePct * positionPct,
		DamagePctProduct:     difficulty.DamagePct * cyclePct * positionPct,
		RewardXPPctProduct:   difficulty.XPPct * rewardPct,
		RewardGoldPctProduct: difficulty.GoldPct * rewardPct,
	}
}

// ScaleStat applies three chained percentages in one floor: value * a * b * c / 1_000_000.
func ScaleStat(value, pctProduct int) int {
	return value * pctProduct / 1_000_000
}

// ScaleReward applies two chained percentages in one floor: value * a * b / 10_000.
func ScaleReward(value, pctProduct int) int {
	return value * pctProduct / 10_000
}
