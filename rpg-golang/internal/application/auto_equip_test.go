package application_test

// Auto-equip with auto-sell (docs/game-design.md §8.1).

import (
	"reflect"
	"slices"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

func autoEngine(t *testing.T, data *domain.GameData, auto bool) *application.GameEngine {
	t.Helper()

	engine, _, err := application.NewRun(data, application.RunConfig{Name: "Auto", VocationID: "warrior", DifficultyID: "normal", AutoEquip: auto}, 42)
	if err != nil {
		t.Fatal(err)
	}

	return engine
}

func item(uid int, itemID, rarity string, tier int) domain.ItemInstance {
	return domain.ItemInstance{UID: uid, ItemID: itemID, Rarity: rarity, Tier: tier, Affixes: []domain.AffixRoll{}}
}

func TestAutoEquip_BetterItemIsEquippedAndTheOldOneSold(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	engine := autoEngine(t, data, true)
	state := engine.State
	starter := state.Player.Equipment[domain.SlotWeapon]
	axe := item(50, "test_axe", "common", 0)
	state.Player.Bag = append(state.Player.Bag, axe)
	gold := state.Player.Gold
	rngState := engine.RngState()

	events := application.AutoEquip(state, data)
	want := []application.Event{
		{"type": "item_auto_equipped", "uid": 50, "itemId": "test_axe", "slot": "weapon", "score": domain.ItemScore(axe, data)},
		{"type": "item_auto_sold", "uid": starter.UID, "itemId": "sword", "gold": domain.ItemValue(starter, data)},
	}

	if !reflect.DeepEqual(events, want) {
		t.Fatalf("events = %v", events)
	}

	if !reflect.DeepEqual(state.Player.Equipment[domain.SlotWeapon], axe) || state.Player.Gold != gold+domain.ItemValue(starter, data) ||
		len(state.Player.Bag) != 0 || engine.RngState() != rngState {
		t.Fatal("the axe is equipped, the sword sold and no randomness consumed")
	}
}

func TestAutoEquip_EmptySlotsAreFilledWithoutSelling(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	state := autoEngine(t, data, true).State
	state.Player.Bag = append(state.Player.Bag, item(60, "test_helmet", "common", 0))

	events := application.AutoEquip(state, data)
	if !reflect.DeepEqual(eventTypes(events), []string{"item_auto_equipped"}) || state.Player.Equipment[domain.SlotHelmet].UID != 60 {
		t.Fatalf("events = %v", events)
	}
}

func TestAutoEquip_TiesGoToTheLowestUIDAndWorseItemsStay(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	state := autoEngine(t, data, true).State
	state.Player.Bag = append(state.Player.Bag, item(72, "test_helmet", "common", 0), item(71, "test_helmet", "common", 0), item(73, "test_rod", "common", 0))
	application.AutoEquip(state, data)

	uids := []int{}
	for _, bagItem := range state.Player.Bag {
		uids = append(uids, bagItem.UID)
	}

	if state.Player.Equipment[domain.SlotHelmet].UID != 71 || !reflect.DeepEqual(uids, []int{72, 73}) {
		t.Fatalf("helmet %d, bag %v", state.Player.Equipment[domain.SlotHelmet].UID, uids)
	}

	if len(application.AutoEquip(state, data)) != 0 {
		t.Fatal("nothing better left")
	}
}

func TestAutoEquip_ItemsAboveThePlayerLevelAreSkipped(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	state := autoEngine(t, data, true).State
	state.Player.Bag = append(state.Player.Bag, item(80, "test_axe", "mythic", 9))

	if len(application.AutoEquip(state, data)) != 0 {
		t.Fatal("level too low")
	}

	state.Player.Level = 1 + 9*data.Balance.ItemLevelPerTier

	if events := application.AutoEquip(state, data); len(events) == 0 || events[0].Int("uid") != 80 {
		t.Fatalf("events = %v", events)
	}
}

func TestAutoEquip_VictoryTriggersAutoEquipOnlyWhenEnabled(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	withEnemyClass(data, "normal", func(row *domain.EnemyClassDef) { row.DropChancePct = 100 })

	for i := range data.Balance.EnemyClasses {
		row := &data.Balance.EnemyClasses[i]
		row.Dodge, row.Parry, row.Crit, row.Heal = 0, 0, 0, 0
	}

	data.Balance.EliteChancePct = 0
	data.Item("sword").Stats = nil

	for _, enabled := range []bool{true, false} {
		engine := autoEngine(t, data, enabled)
		engine.Step(application.NextFight())
		engine.State.Monster.HP = 1
		events := engine.Step(application.Attack())

		if engine.State.Phase != domain.PhaseMerchant || slices.Contains(eventTypes(events), "item_auto_equipped") != enabled {
			t.Fatalf("enabled %v: %v", enabled, eventTypes(events))
		}

		if expected := map[bool]int{true: 1, false: 0}[enabled]; engine.State.Stats.ItemsAutoEquipped != expected {
			t.Fatalf("itemsAutoEquipped = %d", engine.State.Stats.ItemsAutoEquipped)
		}
	}
}

func TestAutoEquip_BuyingAStockItemTriggersAutoEquip(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	engine := autoEngine(t, data, true)
	state := engine.State
	axe := item(90, "test_axe", "common", 0)
	state.MerchantStock = []domain.ItemInstance{axe}
	state.Player.Gold = 10_000

	events := engine.Step(application.BuyStockItem(0))
	if !reflect.DeepEqual(eventTypes(events), []string{"item_bought", "item_auto_equipped", "item_auto_sold"}) || !reflect.DeepEqual(state.Player.Equipment[domain.SlotWeapon], axe) {
		t.Fatalf("events = %v", eventTypes(events))
	}

	manual := autoEngine(t, data, false)
	manual.State.MerchantStock = []domain.ItemInstance{axe}
	manual.State.Player.Gold = 10_000

	if types := eventTypes(manual.Step(application.BuyStockItem(0))); !reflect.DeepEqual(types, []string{"item_bought"}) {
		t.Fatalf("manual = %v", types)
	}
}
