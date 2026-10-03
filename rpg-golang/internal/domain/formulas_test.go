package domain_test

import (
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

func loadData(t *testing.T) *domain.GameData {
	t.Helper()

	data, err := infrastructure.LoadGameData(assets.Shared())
	if err != nil {
		t.Fatalf("load game data: %v", err)
	}

	return data
}

func TestPct(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name           string
		value, percent int
		expected       int
	}{
		{"floors", 99, 50, 49},
		{"zero value", 0, 150, 0},
		{"identity", 7, 100, 7},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if got := domain.Pct(tt.value, tt.percent); got != tt.expected {
				t.Fatalf("Pct(%d, %d) = %d, want %d", tt.value, tt.percent, got, tt.expected)
			}
		})
	}

	t.Run("negative panics", func(t *testing.T) {
		t.Parallel()

		defer func() {
			if recover() == nil {
				t.Fatal("negative operands must panic")
			}
		}()

		domain.Pct(-1, 10)
	})
}

func TestClamp(t *testing.T) {
	t.Parallel()

	if domain.Clamp(5, 0, 3) != 3 || domain.Clamp(-1, 0, 3) != 0 || domain.Clamp(2, 0, 3) != 2 {
		t.Fatal("clamp is wrong")
	}
}

func TestXPForLevel(t *testing.T) {
	t.Parallel()

	tests := []struct{ level, xp int }{{1, 0}, {2, 100}, {3, 200}, {4, 400}, {5, 800}, {10, 9300}, {20, 98800}, {100, 15694800}}
	for _, tt := range tests {
		if got := domain.XPForLevel(tt.level); got != tt.xp {
			t.Fatalf("XPForLevel(%d) = %d, want %d", tt.level, got, tt.xp)
		}
	}
}

func TestManaForMagicLevel(t *testing.T) {
	t.Parallel()

	balance := &loadData(t).Balance
	base := balance.MagicLevelBase

	if domain.ManaForMagicLevel(1, balance) != base {
		t.Fatal("level 1 needs the base mana")
	}

	if domain.ManaForMagicLevel(2, balance) != base+domain.Pct(base, balance.MagicLevelGrowthPct) {
		t.Fatal("magic level mana must be cumulative")
	}
}

func TestSpellLevelForUses(t *testing.T) {
	t.Parallel()

	levels := loadData(t).Balance.SpellLevels
	tests := []struct{ uses, level int }{{0, 1}, {19, 1}, {20, 2}, {49, 2}, {50, 3}, {5000, 3}}

	for _, tt := range tests {
		if got := domain.SpellLevelForUses(tt.uses, levels).Level; got != tt.level {
			t.Fatalf("uses %d → level %d, want %d", tt.uses, got, tt.level)
		}
	}
}

func TestArmorMitigation(t *testing.T) {
	t.Parallel()

	if domain.ArmorMitigation(100, 0) != 100 || domain.ArmorMitigation(100, 100) != 50 || domain.ArmorMitigation(10, 3) != 9 {
		t.Fatal("armor mitigation is wrong")
	}
}

func TestRoundInfoFor(t *testing.T) {
	t.Parallel()

	data := loadData(t)
	tests := []struct {
		round  int
		expect domain.RoundInfo
	}{
		{1, domain.RoundInfo{Round: 1, Tier: 0, Cycle: 0, Position: 0}},
		{10, domain.RoundInfo{Round: 10, Tier: 0, Cycle: 0, Position: 9, IsBoss: true}},
		{11, domain.RoundInfo{Round: 11, Tier: 1, Cycle: 0, Position: 0}},
		{100, domain.RoundInfo{Round: 100, Tier: 9, Cycle: 0, Position: 9, IsBoss: true}},
		{101, domain.RoundInfo{Round: 101, Tier: 0, Cycle: 1, Position: 0}},
		{250, domain.RoundInfo{Round: 250, Tier: 4, Cycle: 2, Position: 9, IsBoss: true}},
	}

	for _, tt := range tests {
		if got := domain.RoundInfoFor(tt.round, &data.Balance, data.TierCount()); got != tt.expect {
			t.Fatalf("RoundInfoFor(%d) = %+v, want %+v", tt.round, got, tt.expect)
		}
	}
}

func TestScalingFor(t *testing.T) {
	t.Parallel()

	data := loadData(t)
	balance := &data.Balance
	hard := balance.MustDifficulty("hard")
	factors := domain.ScalingFor(domain.RoundInfoFor(103, balance, data.TierCount()), balance, hard)
	cyclePct := 100 + balance.CycleStatPct
	positionPct := 100 + 2*balance.PositionPct

	if factors.HPPctProduct != hard.HPPct*cyclePct*positionPct {
		t.Fatal("hp scaling must chain difficulty, cycle and position")
	}

	if domain.ScaleStat(1000, factors.HPPctProduct) != 1000*factors.HPPctProduct/1_000_000 {
		t.Fatal("scale stat must floor once")
	}

	if domain.ScaleReward(100, factors.RewardXPPctProduct) != 100*hard.XPPct*(100+balance.CycleRewardPct)/10_000 {
		t.Fatal("reward scaling is wrong")
	}

	normal := balance.MustDifficulty("normal")

	boss := domain.ScalingFor(domain.RoundInfoFor(10, balance, data.TierCount()), balance, normal)
	if boss.HPPctProduct != normal.HPPct*100*100 {
		t.Fatal("bosses ignore position scaling")
	}
}
