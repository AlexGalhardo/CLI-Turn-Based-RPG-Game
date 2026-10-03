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

// RollRarity is a weighted roll in the order of balance.rarities; zero weights are skipped and a single option is
// not rolled.
func RollRarity(data *domain.GameData, rng *domain.Rng, weights map[string]int) domain.RarityDef {
	var (
		options    []domain.RarityDef
		optWeights []int
	)

	for _, rarity := range data.Balance.Rarities {
		if weight := weights[rarity.ID]; weight > 0 {
			options = append(options, rarity)
			optWeights = append(optWeights, weight)
		}
	}

	switch len(options) {
	case 0:
		panic("rarity table without a positive weight")
	case 1:
		return options[0]
	default:
		return options[rng.Weighted(optWeights)]
	}
}

// ItemRequest describes the item to generate.
type ItemRequest struct {
	Vocation *domain.VocationDef
	Tier     int
	Weights  map[string]int
	UID      int
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
	rarity := RollRarity(data, rng, request.Weights)
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
