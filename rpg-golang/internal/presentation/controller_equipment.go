package presentation

import (
	"strings"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Translation parameter names shared by several screens.
const (
	paramRarity  = "rarity"
	paramStat    = "stat"
	paramScore   = "score"
	paramCurrent = "current"
	paramValue   = "value"
)

// Equipment screens of the controller (docs/tui.md "Equipment screen"): the slot list with scores, the item
// comparison and the equipped-slot detail.

func (c *Controller) usableBag() []domain.ItemInstance {
	data := c.Services.Data
	player := c.Session.State().Player
	vocation := data.Vocation(player.VocationID)
	items := []domain.ItemInstance{}

	for _, item := range player.Bag {
		if application.CanUse(data.Item(item.ItemID), vocation) {
			items = append(items, item)
		}
	}

	return items
}

// scoreDelta is the score of an item minus the score of what is equipped in its slot (0 for an empty slot).
func (c *Controller) scoreDelta(item domain.ItemInstance) int {
	data := c.Services.Data
	delta := domain.ItemScore(item, data)

	if equipped, ok := c.Session.State().Player.Equipment[data.Item(item.ItemID).Slot]; ok {
		delta -= domain.ItemScore(equipped, data)
	}

	return delta
}

// equipmentMenu lists the usable bag items first (keys 1..n, as in docs/tui.md), then the equipped slots.
func (c *Controller) equipmentMenu() []menuEntry {
	data := c.Services.Data
	player := c.Session.State().Player
	entries := []menuEntry{}

	for _, item := range c.usableBag() {
		definition := data.Item(item.ItemID)
		level := domain.RequiredLevel(item, data)
		label := c.T("equipment.bag_option", map[string]any{
			"name": definition.Name, paramRarity: c.T("rarity."+item.Rarity, nil), "slot": c.T("slot."+string(definition.Slot), nil),
			"level": level, paramScore: domain.ItemScore(item, data),
		})

		color := item.Rarity

		if level > player.Level {
			label += " · " + c.T("equipment.requires_level", map[string]any{"level": level})
			color = StyleDim
		}

		delta := c.scoreDelta(item)
		option := MenuOption{Label: label, Color: color, Detail: FormatDelta(delta), DetailColor: DeltaStyle(delta)}
		entries = append(entries, menuEntry{option, c.openCompare(item.UID)})
	}

	for _, slot := range domain.EquipmentSlotOrder {
		equipped, ok := player.Equipment[slot]
		if !ok {
			continue
		}

		label := c.T("equipment.slot_option", map[string]any{
			"slot": c.T("slot."+string(slot), nil), "name": data.Item(equipped.ItemID).Name, paramRarity: c.T("rarity."+equipped.Rarity, nil),
		})
		entries = append(entries, menuEntry{MenuOption{Label: label, Color: equipped.Rarity}, c.openSlot(slot)})
	}

	for index := range entries {
		entries[index].option.Key = ListKey(index)
	}

	return entries
}

func (c *Controller) openCompare(uid int) func() {
	return func() {
		c.compareUID = uid
		c.View = ViewCompare
	}
}

func (c *Controller) openSlot(slot domain.Slot) func() {
	return func() {
		c.slot = slot
		c.View = ViewEquippedSlot
	}
}

func (c *Controller) comparedItem() (domain.ItemInstance, bool) {
	for _, item := range c.Session.State().Player.Bag {
		if item.UID == c.compareUID {
			return item, true
		}
	}

	return domain.ItemInstance{}, false
}

func (c *Controller) equipCompared() {
	c.step(application.Equip(c.compareUID))
	c.View = ViewEquipment
}

func (c *Controller) unequipSlot() {
	c.step(application.Unequip(c.slot))
	c.View = ViewEquipment
}

func (c *Controller) styledBody() []BodyLine {
	switch c.View {
	case ViewEquipment:
		return c.equipmentBody()
	case ViewCompare:
		return c.compareBody()
	default:
		return c.slotBody()
	}
}

func (c *Controller) equipmentBody() []BodyLine {
	data := c.Services.Data
	player := c.Session.State().Player
	lines := []BodyLine{{Text: c.T("equipment.equipped_header", map[string]any{paramScore: domain.EquipmentScore(player, data)})}}

	for _, slot := range domain.EquipmentSlotOrder {
		slotName := c.T("slot."+string(slot), nil)

		item, ok := player.Equipment[slot]
		if !ok {
			lines = append(lines, BodyLine{c.T("equipment.slot_empty", map[string]any{"slot": slotName}), StyleWarning})

			continue
		}

		text := c.T("equipment.slot_line", map[string]any{
			"slot": slotName, "name": data.Item(item.ItemID).Name, paramRarity: c.T("rarity."+item.Rarity, nil),
			"level": domain.RequiredLevel(item, data), paramScore: domain.ItemScore(item, data),
		})
		lines = append(lines, BodyLine{text, item.Rarity})
	}

	lines = append(lines, BodyLine{}, BodyLine{Text: c.T("equipment.bag_header", nil)})
	if len(c.usableBag()) == 0 {
		lines = append(lines, BodyLine{Text: c.T("equipment.bag_empty", nil)})
	}

	return lines
}

func (c *Controller) compareTitle() string {
	item, ok := c.comparedItem()
	if !ok {
		return c.T("merchant.equipment", nil)
	}

	data := c.Services.Data
	slot := data.Item(item.ItemID).Slot

	current := c.T("equipment.empty", nil)
	if equipped, found := c.Session.State().Player.Equipment[slot]; found {
		current = data.Item(equipped.ItemID).Name
	}

	return c.T("equipment.compare_title", map[string]any{
		"slot": c.T("slot."+string(slot), nil), paramCurrent: current, "new": data.Item(item.ItemID).Name,
	})
}

func (c *Controller) affixList(item domain.ItemInstance) string {
	parts := make([]string, len(item.Affixes))
	for i, affix := range item.Affixes {
		parts[i] = c.T("equipment.affix", map[string]any{paramValue: affix.Value, paramStat: c.T("stat."+string(affix.Stat), nil)})
	}

	return strings.Join(parts, ", ")
}

func (c *Controller) compareBody() []BodyLine {
	item, ok := c.comparedItem()
	if !ok {
		return []BodyLine{}
	}

	data := c.Services.Data
	player := c.Session.State().Player
	current, equipped := player.Equipment[data.Item(item.ItemID).Slot]
	newStats := domain.ItemStats(item, data)

	oldStats := map[domain.Stat]int{}
	if equipped {
		oldStats = domain.ItemStats(current, data)
	}

	lines := []BodyLine{}

	for _, stat := range domain.Stats {
		newValue, hasNew := newStats[stat]

		oldValue, hasOld := oldStats[stat]
		if !hasNew && !hasOld {
			continue
		}

		text := c.T("equipment.stat_delta", map[string]any{
			paramStat: c.T("stat."+string(stat), nil), paramCurrent: oldValue, "new": newValue, "delta": FormatDelta(newValue - oldValue),
		})
		lines = append(lines, BodyLine{text, DeltaStyle(newValue - oldValue)})
	}

	if gained := c.affixList(item); gained != "" {
		lines = append(lines, BodyLine{c.T("equipment.affixes_gained", map[string]any{"affixes": gained}), StyleGain})
	}

	if equipped {
		if lost := c.affixList(current); lost != "" {
			lines = append(lines, BodyLine{c.T("equipment.affixes_lost", map[string]any{"affixes": lost}), StyleLoss})
		}
	}

	oldScore := 0
	if equipped {
		oldScore = domain.ItemScore(current, data)
	}

	newScore := domain.ItemScore(item, data)
	score := c.T("equipment.score_delta", map[string]any{paramCurrent: oldScore, "new": newScore, "delta": FormatDelta(newScore - oldScore)})
	lines = append(lines, BodyLine{score, DeltaStyle(newScore - oldScore)})

	if level := domain.RequiredLevel(item, data); level > player.Level {
		lines = append(lines, BodyLine{c.T("equipment.level_needed", map[string]any{"level": level, paramCurrent: player.Level}), StyleLoss})
	}

	return lines
}

func (c *Controller) slotBody() []BodyLine {
	data := c.Services.Data

	item, ok := c.Session.State().Player.Equipment[c.slot]
	if !ok {
		return []BodyLine{{c.T("equipment.empty", nil), StyleWarning}}
	}

	lines := []BodyLine{{
		c.T("equipment.item_title", map[string]any{
			"name": data.Item(item.ItemID).Name, paramRarity: c.T("rarity."+item.Rarity, nil),
			"level": domain.RequiredLevel(item, data), paramScore: domain.ItemScore(item, data),
		}),
		item.Rarity,
	}}
	stats := domain.ItemStats(item, data)

	for _, stat := range domain.Stats {
		if value, has := stats[stat]; has {
			lines = append(lines, BodyLine{Text: c.T("character.stat_line", map[string]any{paramStat: c.T("stat."+string(stat), nil), paramValue: value})})
		}
	}

	return lines
}
