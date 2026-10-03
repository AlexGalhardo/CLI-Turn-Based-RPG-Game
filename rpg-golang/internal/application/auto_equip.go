package application

import (
	"slices"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// BestBagItem returns the highest-score bag item the player can wear in a slot now; ties go to the lowest uid.
func BestBagItem(state *RunState, data *domain.GameData, slot domain.Slot) (domain.ItemInstance, bool) {
	player := state.Player
	vocation := data.Vocation(player.VocationID)

	var (
		best      domain.ItemInstance
		bestScore int
		found     bool
	)

	for _, item := range player.Bag {
		definition := data.Item(item.ItemID)
		if definition.Slot != slot || !CanUse(definition, vocation) || domain.RequiredLevel(item, data) > player.Level {
			continue
		}

		score := domain.ItemScore(item, data)
		if !found || score > bestScore || (score == bestScore && item.UID < best.UID) {
			best, bestScore, found = item, score, true
		}
	}

	return best, found
}

// AutoEquip equips the best bag item of every slot and sells the replaced one (docs/game-design.md §8.1).
// It consumes no randomness.
func AutoEquip(state *RunState, data *domain.GameData) []Event {
	player := state.Player
	events := []Event{}

	for _, slot := range domain.EquipmentSlotOrder {
		best, ok := BestBagItem(state, data, slot)
		if !ok {
			continue
		}

		score := domain.ItemScore(best, data)

		current, equipped := player.Equipment[slot]
		if equipped && score <= domain.ItemScore(current, data) {
			continue
		}

		player.Bag = slices.DeleteFunc(player.Bag, func(item domain.ItemInstance) bool { return item.UID == best.UID })
		player.Equipment[slot] = best
		events = append(events, NewEvent("item_auto_equipped", map[string]any{
			"uid": best.UID, "itemId": best.ItemID, "slot": string(slot), "score": score,
		}))

		if equipped {
			gold := domain.ItemValue(current, data)
			player.Gold += gold
			events = append(events, NewEvent("item_auto_sold", map[string]any{"uid": current.UID, "itemId": current.ItemID, "gold": gold}))
		}
	}

	if len(events) > 0 {
		sheet := domain.BuildSheet(player, data)
		player.HP = min(player.HP, sheet.MaxHP)
		player.MP = min(player.MP, sheet.MaxMP)
	}

	return events
}
