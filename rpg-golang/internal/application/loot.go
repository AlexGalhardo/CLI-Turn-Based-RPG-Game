package application

import (
	"slices"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// CanUse reports whether a vocation can equip an item (docs/game-design.md §8).
func CanUse(item *domain.ItemDef, vocation *domain.VocationDef) bool {
	switch item.Slot {
	case domain.SlotWeapon:
		return slices.Contains(vocation.WeaponTypes, item.Type)
	case domain.SlotShield:
		return slices.Contains(vocation.ShieldTypes, item.Type)
	default:
		return true
	}
}

// RarityWeights returns the rarity weights of a drop table, with the difficulty bonus on non-common rarities.
func RarityWeights(data *domain.GameData, table string, difficulty domain.DifficultyDef) []int {
	weights := data.Balance.RarityWeights[table]
	result := make([]int, 0, len(data.Balance.Rarities))

	for _, rarity := range data.Balance.Rarities {
		weight := weights[rarity.ID]
		if rarity.ID != "common" {
			weight = domain.Pct(weight, difficulty.NonCommonWeightPct)
		}

		result = append(result, weight)
	}

	return result
}

// ItemRequest describes the item to generate.
type ItemRequest struct {
	Vocation   *domain.VocationDef
	Tier       int
	Table      string
	Difficulty domain.DifficultyDef
	UID        int
}

// GenerateItem builds base item + rarity + affixes. It returns false (consuming no randomness) when no item fits.
func GenerateItem(data *domain.GameData, rng *domain.Rng, request ItemRequest) (domain.ItemInstance, bool) {
	lowestTier := max(0, request.Tier-1)

	var candidates []*domain.ItemDef

	for i := range data.Items {
		item := &data.Items[i]
		if lowestTier <= item.Tier && item.Tier <= request.Tier && CanUse(item, request.Vocation) {
			candidates = append(candidates, item)
		}
	}

	if len(candidates) == 0 {
		return domain.ItemInstance{}, false
	}

	slices.SortFunc(candidates, func(a, b *domain.ItemDef) int { return domain.ByID(a.ID, b.ID) })
	base := domain.Pick(rng, candidates)
	rarity := data.Balance.Rarities[rng.Weighted(RarityWeights(data, request.Table, request.Difficulty))]
	affixCount := rng.Roll(rarity.AffixMin, rarity.AffixMax)

	rolls := []domain.AffixRoll{}
	usedStats := map[domain.Stat]bool{}

	for range affixCount {
		var pool []*domain.AffixDef

		for i := range data.Affixes {
			affix := &data.Affixes[i]
			if slices.Contains(affix.Slots, base.Slot) && !usedStats[affix.Stat] {
				pool = append(pool, affix)
			}
		}

		if len(pool) == 0 {
			break
		}

		slices.SortFunc(pool, func(a, b *domain.AffixDef) int { return domain.ByID(a.ID, b.ID) })
		affix := domain.Pick(rng, pool)
		usedStats[affix.Stat] = true
		rolls = append(rolls, domain.AffixRoll{Stat: affix.Stat, Value: rng.Roll(affix.Min, affix.Max) + request.Tier*affix.PerTier})
	}

	return domain.ItemInstance{UID: request.UID, ItemID: base.ID, Rarity: rarity.ID, Tier: request.Tier, Affixes: rolls}, true
}
