// Package presentation holds the framework-independent UI: event texts, render helpers, the UI controller,
// CLI flags and the simulator report. The Bubble Tea renderer lives in presentation/tui.
package presentation

import (
	"fmt"
	"maps"
	"strconv"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

// nameFields are event fields holding ids, replaced by display names before formatting.
var nameFields = []struct{ field, name string }{
	{"spellId", "spell"}, {"potionId", "potion"}, {"monsterId", "monster"}, {"itemId", "item"},
}

// EventFormatter turns engine events into translated sentences.
type EventFormatter struct {
	data       *domain.GameData
	translator *infrastructure.Translator
}

// NewEventFormatter creates a formatter.
func NewEventFormatter(data *domain.GameData, translator *infrastructure.Translator) *EventFormatter {
	return &EventFormatter{data: data, translator: translator}
}

// Format renders one event.
func (f *EventFormatter) Format(evt application.Event, state *application.RunState) string {
	params := map[string]any{}
	maps.Copy(params, evt)

	for _, entry := range nameFields {
		if value, ok := evt[entry.field]; ok {
			params[entry.name] = f.displayName(entry.field, fmt.Sprint(value))
		}
	}

	for _, field := range []string{"element", "status", paramRarity, "resource"} {
		if value, ok := evt[field]; ok {
			params[field] = f.translator.T(field+"."+fmt.Sprint(value), nil)
		}
	}

	if _, ok := params["monster"]; !ok && state.Monster != nil {
		params["monster"] = f.data.Creature(state.Monster.CreatureID).Name
	}

	if uid, ok := evt["uid"]; ok {
		if _, has := params["item"]; !has {
			params["item"] = itemNameByUID(f.data, fmt.Sprint(uid), state)
		}
	}

	return f.translator.T(eventKey(evt), params)
}

func eventKey(evt application.Event) string {
	if evt.Type() == "error" {
		return "error." + evt.Str("code")
	}

	variant := ""

	switch {
	case evt.Bool("crit"):
		variant = "_crit"
	case evt.Bool("charged"):
		variant = "_charged"
	case evt.Type() == "round_started" && evt.Bool("isBoss"):
		variant = "_boss"
	case evt.Type() == "round_started" && evt.Str("enemyClass") == "elite":
		variant = "_elite"
	case evt.Str("target") != "":
		variant = "_" + evt.Str("target")
	}

	return "event." + evt.Type() + variant
}

func (f *EventFormatter) displayName(field, identifier string) string {
	switch field {
	case "spellId":
		if f.data.HasSpell(identifier) {
			return f.data.Spell(identifier).Name
		}
	case "potionId":
		if f.data.HasPotion(identifier) {
			return f.data.Potion(identifier).Name
		}
	case "monsterId":
		if f.data.HasCreature(identifier) {
			return f.data.Creature(identifier).Name
		}
	default:
		if f.data.HasItem(identifier) {
			return f.data.Item(identifier).Name
		}
	}

	return identifier
}

func itemNameByUID(data *domain.GameData, uid string, state *application.RunState) string {
	player := state.Player
	items := append([]domain.ItemInstance{}, player.Bag...)

	for _, slot := range domain.Slots {
		if item, ok := player.Equipment[slot]; ok {
			items = append(items, item)
		}
	}

	items = append(items, state.MerchantStock...)
	for _, item := range items {
		if strconv.Itoa(item.UID) == uid {
			return data.Item(item.ItemID).Name
		}
	}

	return "#" + uid
}
