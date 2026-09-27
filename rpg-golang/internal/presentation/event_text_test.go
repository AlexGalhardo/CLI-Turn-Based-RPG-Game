package presentation_test

import (
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
)

func TestEventFormatter_Format(t *testing.T) {
	t.Parallel()

	data := testData(t)
	translator, _ := infrastructure.NewTranslator(assets.Shared(), "en")
	formatter := presentation.NewEventFormatter(data, translator)

	engine, _, err := application.NewRun(data, application.RunConfig{Name: "T", VocationID: "warrior", DifficultyID: "normal"}, 42)
	if err != nil {
		t.Fatal(err)
	}

	tests := []struct {
		name     string
		event    application.Event
		expected string
	}{
		{"attack", application.Event{"type": "player_attacked", "damage": 12, "crit": false, "element": "fire"}, "You hit for 12 fire damage."},
		{"crit", application.Event{"type": "player_attacked", "damage": 30, "crit": true, "element": "physical"}, "CRITICAL! You hit for 30 physical damage."},
		{"spell", application.Event{"type": "spell_cast", "spellId": "flame_strike", "damage": 9, "crit": false, "element": "fire", "mana": 20}, "Flame Strike deals 9 fire damage."},
		{"potion", application.Event{"type": "potion_used", "potionId": "mana_potion", "amount": 80, "resource": "mp"}, "Mana Potion restores 80 MP."},
		{"status", application.Event{"type": "status_applied", "target": "player", "status": "burn", "turns": 3, "perTurn": 2}, "You are burning (3 turns)."},
		{"kill", application.Event{"type": "monster_killed", "monsterId": "dragon", "isBoss": false}, "You defeated Dragon!"},
		{"boss round", application.Event{"type": "round_started", "round": 10, "tier": 0, "cycle": 0, "monsterId": "munster", "isBoss": true, "hp": 5}, "Round 10: the boss Munster challenges you! (5 HP)"},
		{"sold", application.Event{"type": "item_sold", "uid": 3, "itemId": "sword", "gold": 25}, "You sold Sword for 25 gold."},
		{"charged", application.Event{"type": "monster_attacked", "monsterId": "rat", "attackId": "melee", "damage": 5, "element": "physical", "charged": true}, "Rat unleashes its power: 5 physical damage!"},
		{"error", application.Event{"type": "error", "code": "not_enough_mana"}, "Not enough mana."},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if got := formatter.Format(tt.event, engine.State); got != tt.expected {
				t.Fatalf("got %q, want %q", got, tt.expected)
			}
		})
	}

	state := engine.State
	if !strings.Contains(formatter.Format(application.Event{"type": "spell_cast", "spellId": "ghost", "damage": 1, "crit": false, "element": "fire", "mana": 1}, state), "ghost") {
		t.Fatal("unknown ids render as themselves")
	}

	state.Player.Bag = append(state.Player.Bag, domain.ItemInstance{UID: 77, ItemID: "bow", Rarity: "rare"})
	if formatter.Format(application.Event{"type": "item_equipped", "uid": 77, "slot": "weapon"}, state) != "You equipped Bow." ||
		!strings.Contains(formatter.Format(application.Event{"type": "item_equipped", "uid": 999, "slot": "ring"}, state), "#999") {
		t.Fatal("items are named by uid")
	}

	unknown := application.Event{"type": "potion_used", "potionId": "x", "amount": 1, "resource": "hp"}
	if !strings.Contains(formatter.Format(unknown, state), "x") {
		t.Fatal("unknown potion id kept")
	}
}
