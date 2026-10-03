package presentation_test

import (
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
)

func TestEventFormatter_M8Events(t *testing.T) {
	t.Parallel()

	data := testData(t)
	translator, _ := infrastructure.NewTranslator(assets.Shared(), "en")
	formatter := presentation.NewEventFormatter(data, translator)

	engine, _, err := application.NewRun(data, application.RunConfig{Name: "T", VocationID: "warrior", DifficultyID: "normal"}, 42)
	if err != nil {
		t.Fatal(err)
	}

	state := engine.State
	state.Monster = &domain.MonsterInstance{CreatureID: "rat", EnemyClass: "normal", HP: 10, MaxHP: 10, XP: 1, GoldMin: 1, GoldMax: 1}

	tests := []struct {
		event    application.Event
		expected string
	}{
		{application.Event{"type": "error", "code": "level_too_low"}, "Your level is too low for that item."},
		{application.Event{"type": "round_started", "round": 3, "tier": 0, "cycle": 0, "monsterId": "rat", "isBoss": false, "enemyClass": "elite", "hp": 90}, "Round 3: an ELITE Rat appears! (90 HP)"},
		{application.Event{"type": "monster_attacked", "attackId": "bite", "damage": 9, "element": "physical", "charged": false, "crit": true}, "CRITICAL! Rat hits you for 9 physical damage."},
		{application.Event{"type": "monster_dodged"}, "Rat dodges your attack!"},
		{application.Event{"type": "monster_parried", "reflected": 4}, "Rat parries your attack: you take 4 damage!"},
		{application.Event{"type": "monster_healed", "amount": 12}, "Rat heals 12 HP."},
		{application.Event{"type": "attack_parried", "attackId": "bite", "reflected": 3}, "You parry the attack and reflect 3 damage!"},
		{application.Event{"type": "item_auto_equipped", "uid": 5, "itemId": "sword", "slot": "weapon", "score": 60}, "Auto-equipped Sword (score 60)."},
		{application.Event{"type": "item_auto_sold", "uid": 6, "itemId": "bow", "gold": 30}, "Sold Bow for 30 gold (auto-sell)."},
		{application.Event{"type": "potion_dropped", "potionId": "mana_potion"}, "Loot: Mana Potion!"},
		{application.Event{"type": "run_won", "round": 100}, "VICTORY! You defeated the final boss on round 100!"},
		{application.Event{"type": "run_ended", "won": true}, "Your victory is recorded in the Hall of Fame."},
	}
	for _, tt := range tests {
		if got := formatter.Format(tt.event, state); got != tt.expected {
			t.Fatalf("%s: got %q, want %q", tt.event.Type(), got, tt.expected)
		}
	}
}

func TestFormatDeltaAndDeltaStyle(t *testing.T) {
	t.Parallel()

	if presentation.FormatDelta(3) != "+3" || presentation.FormatDelta(-2) != "-2" || presentation.FormatDelta(0) != "0" {
		t.Fatal("signed deltas")
	}

	if presentation.DeltaStyle(1) != presentation.StyleGain || presentation.DeltaStyle(-1) != presentation.StyleLoss || presentation.DeltaStyle(0) != "" {
		t.Fatal("delta styles")
	}
}
