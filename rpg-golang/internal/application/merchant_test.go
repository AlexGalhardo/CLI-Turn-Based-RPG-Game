package application_test

import (
	"reflect"
	"slices"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

func withTestItems(t *testing.T) *domain.GameData {
	t.Helper()

	data := freshData(t)
	data.Items = append(data.Items,
		domain.ItemDef{ID: "test_helmet", Name: "Test Helmet", Slot: domain.SlotHelmet, Type: "helmet", Stats: []domain.StatValue{{Stat: domain.StatArmor, Value: 10}, {Stat: domain.StatMaxHp, Value: 50}}, Value: 100},
		domain.ItemDef{ID: "test_ring", Name: "Test Ring", Slot: domain.SlotRing, Type: "ring", Stats: []domain.StatValue{{Stat: domain.StatDodge, Value: 1}}, Value: 100},
		domain.ItemDef{ID: "test_axe", Name: "Test Axe", Slot: domain.SlotWeapon, Type: "axe", Stats: []domain.StatValue{{Stat: domain.StatAttack, Value: 20}}, Value: 100},
		domain.ItemDef{ID: "test_rod", Name: "Test Rod", Slot: domain.SlotWeapon, Type: "rod", Stats: []domain.StatValue{{Stat: domain.StatAttack, Value: 1}}, Value: 100},
	)
	data.Affixes = []domain.AffixDef{
		{ID: "of_power", Stat: domain.StatAttack, Min: 1, Max: 3, PerTier: 2, Slots: []domain.Slot{domain.SlotWeapon}},
		{ID: "of_the_bear", Stat: domain.StatMaxHp, Min: 5, Max: 10, PerTier: 5, Slots: []domain.Slot{domain.SlotWeapon, domain.SlotHelmet}},
		{ID: "of_speed", Stat: domain.StatDodge, Min: 1, Max: 2, Slots: []domain.Slot{domain.SlotWeapon, domain.SlotRing}},
		{ID: "of_speed_2", Stat: domain.StatDodge, Min: 1, Max: 2, Slots: []domain.Slot{domain.SlotWeapon}},
	}

	return data.Index()
}

func TestMerchant_BuyPotion(t *testing.T) {
	t.Parallel()

	data := testData(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	gold := engine.State.Player.Gold

	if evt := engine.Step(application.BuyPotion("health_potion", 2))[0]; evt.Type() != "potion_bought" || evt.Int("gold") != 100 {
		t.Fatalf("purchase failed: %v", evt)
	}

	if engine.State.Player.Gold != gold-100 || engine.State.Stats.PotionsBought["health_potion"] != 2 {
		t.Fatal("gold and statistics must update")
	}

	tests := []struct {
		name    string
		command application.Command
		code    string
	}{
		{"not enough gold", application.BuyPotion("health_potion", 1), application.ErrNotEnoughGold},
		{"zero quantity", application.BuyPotion("health_potion", 0), application.ErrInvalidQuantity},
		{"locked potion", application.BuyPotion("great_health_potion", 1), application.ErrPotionLocked},
		{"unknown potion", application.BuyPotion("elixir", 1), application.ErrUnknownPotion},
	}
	for _, tt := range tests {
		if got := engine.Step(tt.command)[0]; got.Str("code") != tt.code {
			t.Fatalf("%s: got %v", tt.name, got)
		}
	}

	if !reflect.DeepEqual(application.AvailablePotions(engine.State, data), []string{"health_potion", "mana_potion"}) {
		t.Fatal("only starter potions at round 0")
	}

	engine.State.Round = 80
	if len(application.AvailablePotions(engine.State, data)) != len(data.Potions) {
		t.Fatal("every potion unlocks by round 80")
	}
}

func TestMerchant_Equipment(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	player := engine.State.Player
	rod := domain.ItemInstance{UID: 51, ItemID: "test_rod", Rarity: "rare", Affixes: []domain.AffixRoll{}}
	player.Bag = append(player.Bag, domain.ItemInstance{UID: 50, ItemID: "test_axe", Rarity: "common", Affixes: []domain.AffixRoll{}}, rod)

	if engine.Step(application.Equip(51))[0].Str("code") != application.ErrCannotEquip {
		t.Fatal("a warrior cannot equip a rod")
	}

	if got := types(engine.Step(application.Equip(50))); !reflect.DeepEqual(got, []string{"item_unequipped", "item_equipped"}) {
		t.Fatalf("swap events = %v", got)
	}

	if domain.BuildSheet(player, data).MeleeMin != 8+20 {
		t.Fatal("the axe adds its attack")
	}

	if evt := engine.Step(application.SellItem(51))[0]; evt.Int("gold") != domain.ItemValue(rod, data) {
		t.Fatalf("sell = %v", evt)
	}

	for _, command := range []application.Command{application.SellItem(51), application.Equip(999)} {
		if engine.Step(command)[0].Str("code") != application.ErrInvalidItem {
			t.Fatal("unknown uids are invalid")
		}
	}

	if engine.Step(application.Unequip(domain.SlotWeapon))[0].Type() != "item_unequipped" ||
		engine.Step(application.Unequip(domain.SlotWeapon))[0].Str("code") != application.ErrInvalidItem {
		t.Fatal("unequip then nothing to unequip")
	}

	player.Equipment[domain.SlotHelmet] = domain.ItemInstance{UID: 70, ItemID: "test_helmet", Rarity: "common"}
	player.HP = domain.BuildSheet(player, data).MaxHP

	for len(player.Bag) < data.Balance.BagCapacity {
		player.Bag = append(player.Bag, domain.ItemInstance{UID: 100 + len(player.Bag), ItemID: "test_ring", Rarity: "common"})
	}

	if engine.Step(application.Unequip(domain.SlotHelmet))[0].Str("code") != application.ErrBagFull {
		t.Fatal("full bag blocks unequip")
	}

	player.Bag = player.Bag[:len(player.Bag)-1]

	engine.Step(application.Unequip(domain.SlotHelmet))

	if player.HP != domain.BuildSheet(player, data).MaxHP {
		t.Fatal("hp is clamped to the new maximum")
	}
}

func TestMerchant_Stock(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	state := engine.State

	if len(state.MerchantStock) != data.Balance.MerchantStockSize {
		t.Fatalf("stock size = %d", len(state.MerchantStock))
	}

	item := state.MerchantStock[0]
	state.Player.Gold = application.StockPrice(item, data)

	if evt := engine.Step(application.BuyStockItem(0))[0]; evt.Type() != "item_bought" || !slices.ContainsFunc(state.Player.Bag, func(i domain.ItemInstance) bool { return i.UID == item.UID }) {
		t.Fatalf("buy failed: %v", evt)
	}

	if engine.Step(application.BuyStockItem(0))[0].Str("code") != application.ErrNotEnoughGold ||
		engine.Step(application.BuyStockItem(9))[0].Str("code") != application.ErrInvalidItem {
		t.Fatal("gold and index are validated")
	}

	for len(state.Player.Bag) < data.Balance.BagCapacity {
		state.Player.Bag = append(state.Player.Bag, domain.ItemInstance{UID: 200 + len(state.Player.Bag), ItemID: "test_ring", Rarity: "common"})
	}

	if engine.Step(application.BuyStockItem(0))[0].Str("code") != application.ErrBagFull {
		t.Fatal("full bag blocks purchases")
	}

	engine.Step(application.NextFight())

	if len(state.MerchantStock) != 0 {
		t.Fatal("stock is cleared when leaving")
	}
}

func TestGenerateItem(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	vocation := data.Vocation("warrior")
	boss := data.Balance.MustEnemyClass("boss").RarityWeights
	generate := func(rng *domain.Rng, tier, uid int) (domain.ItemInstance, bool) {
		return application.GenerateItem(data, rng, application.ItemRequest{Vocation: vocation, Tier: tier, Weights: boss, UID: uid})
	}

	first, _ := generate(domain.NewRng(5), 0, 1)
	second, _ := generate(domain.NewRng(5), 0, 1)

	if !reflect.DeepEqual(first, second) {
		t.Fatal("generation must be deterministic")
	}

	rng := domain.NewRng(11)
	for uid := range 200 {
		item, ok := generate(rng, 1, uid)
		if !ok || item.Rarity == "common" || !application.CanUse(data.Item(item.ItemID), vocation) {
			t.Fatalf("bad boss item: %+v", item)
		}

		seen := map[domain.Stat]bool{}
		for _, affix := range item.Affixes {
			if seen[affix.Stat] {
				t.Fatal("affix stats must be unique")
			}

			seen[affix.Stat] = true
		}
	}

	empty := freshData(t)
	empty.Items = nil
	empty.Index()

	emptyRng := domain.NewRng(3)

	normalWeights := data.Balance.MustEnemyClass("normal").RarityWeights
	if _, ok := application.GenerateItem(empty, emptyRng, application.ItemRequest{Vocation: vocation, Tier: 9, Weights: normalWeights}); ok || emptyRng.State() != 3 {
		t.Fatal("no candidates consumes nothing")
	}
}
