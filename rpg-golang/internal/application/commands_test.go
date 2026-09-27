package application_test

import (
	"encoding/json"
	"reflect"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

func TestCommandFromJSON(t *testing.T) {
	t.Parallel()

	commands := []application.Command{
		application.Attack(), application.Cast("brutal_strike"), application.UsePotion("health_potion"), application.Defend(),
		application.NextFight(), application.BuyPotion("mana_potion", 3), application.SellItem(4), application.Equip(5),
		application.Unequip(domain.SlotRing), application.BuyStockItem(1),
	}

	for _, command := range commands {
		t.Run(command.Type, func(t *testing.T) {
			t.Parallel()

			raw, err := json.Marshal(command.ToMap())
			if err != nil {
				t.Fatal(err)
			}

			parsed, err := application.CommandFromJSON(raw)
			if err != nil || !reflect.DeepEqual(parsed, command) {
				t.Fatalf("round trip: %+v, %v", parsed, err)
			}
		})
	}

	for _, raw := range []string{`{"type":"dance"}`, `not json`} {
		if _, err := application.CommandFromJSON([]byte(raw)); err == nil {
			t.Fatalf("%s must fail", raw)
		}
	}
}

func TestRunStateFromJSON(t *testing.T) {
	t.Parallel()

	engine := newEngine(t, testData(t), "warrior", "normal", 42)
	engine.Step(application.NextFight())
	engine.Step(application.Attack())
	engine.State.Player.Bag = append(engine.State.Player.Bag, domain.ItemInstance{UID: 90, ItemID: "sword", Rarity: "epic", Tier: 2, Affixes: []domain.AffixRoll{{Stat: domain.StatDodge, Value: 3}}})

	clone := engine.State.Clone()
	if !reflect.DeepEqual(clone, engine.State) {
		t.Fatal("clone must equal the original")
	}

	clone.Player.Gold = 999
	if engine.State.Player.Gold == 999 {
		t.Fatal("clone must be deep")
	}

	for _, raw := range []string{`{`, `{"seed":1}`} {
		if _, err := application.RunStateFromJSON([]byte(raw)); err == nil {
			t.Fatalf("%s must fail", raw)
		}
	}
}
