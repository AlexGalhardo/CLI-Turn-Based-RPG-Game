package domain_test

import (
	"errors"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

func withTestItems(data *domain.GameData) *domain.GameData {
	data.Items = append(data.Items,
		domain.ItemDef{ID: "test_helmet", Name: "Test Helmet", Slot: domain.SlotHelmet, Type: "helmet", Stats: []domain.StatValue{{Stat: domain.StatArmor, Value: 10}, {Stat: domain.StatMaxHp, Value: 50}}, Value: 100},
		domain.ItemDef{ID: "test_ring", Name: "Test Ring", Slot: domain.SlotRing, Type: "ring", Stats: []domain.StatValue{{Stat: domain.StatCritChance, Value: 80}, {Stat: domain.StatDodge, Value: 90}}, Value: 100},
	)

	return data.Index()
}

func TestItemStats(t *testing.T) {
	t.Parallel()

	data := withTestItems(loadData(t))
	item := domain.ItemInstance{UID: 1, ItemID: "test_helmet", Rarity: "legendary", Affixes: []domain.AffixRoll{{Stat: domain.StatMaxHp, Value: 7}}}
	stats := domain.ItemStats(item, data)

	if stats[domain.StatArmor] != 20 || stats[domain.StatMaxHp] != 107 {
		t.Fatalf("legendary stats wrong: %v", stats)
	}

	if domain.ItemValue(item, data) != 600 {
		t.Fatal("legendary value must be 600%")
	}
}

func TestBuildSheet(t *testing.T) {
	t.Parallel()

	data := withTestItems(loadData(t))
	player := domain.NewPlayer("A", "warrior", 10, 10, 0)
	player.Equipment[domain.SlotRing] = domain.ItemInstance{UID: 2, ItemID: "test_ring", Rarity: "common"}
	sheet := domain.BuildSheet(player, data)

	if sheet.CritChance != data.Balance.Caps.CritChance || sheet.Dodge != data.Balance.Caps.Dodge {
		t.Fatalf("caps must apply: %+v", sheet)
	}

	if sheet.WeaponElement != domain.Physical || sheet.Protection(domain.Fire) != 0 {
		t.Fatal("an unarmed character deals physical damage and has no protection")
	}

	player.Equipment[domain.SlotWeapon] = domain.ItemInstance{UID: 3, ItemID: "wand_of_vortex", Rarity: "common"}
	if domain.BuildSheet(player, data).WeaponElement != domain.Energy {
		t.Fatal("weapons set the melee element")
	}
}

func TestGameDataLookups(t *testing.T) {
	t.Parallel()

	data := loadData(t)

	expectUnknown := func(name string, lookup func()) {
		t.Helper()

		defer func() {
			if err, ok := recover().(error); !ok || !errors.As(err, new(domain.UnknownIDError)) {
				t.Fatalf("%s must panic with UnknownIDError", name)
			}
		}()

		lookup()
	}

	expectUnknown("spell", func() { data.Spell("avada_kedavra") })
	expectUnknown("boss", func() { data.BossOfTier(99) })

	if _, err := data.Balance.Difficulty("nightmare"); !errors.As(err, new(domain.UnknownIDError)) {
		t.Fatal("unknown difficulty must fail")
	}

	if _, err := data.Balance.Rarity("epic"); err == nil {
		t.Fatal("unknown rarity must fail")
	}

	if _, err := data.Balance.EnemyClass("champion"); err == nil {
		t.Fatal("unknown enemy class must fail")
	}

	if _, err := data.Balance.AutoBattle.Mode("berserk"); err == nil {
		t.Fatal("unknown auto-battle mode must fail")
	}

	expectUnknown("enemy class", func() { data.Balance.MustEnemyClass("champion") })

	if _, err := data.Creature("rat").Attack("laser"); err == nil {
		t.Fatal("unknown attack must fail")
	}

	if len(data.MonstersInTier(99)) != 0 || domain.ByID("a", "b") >= 0 {
		t.Fatal("empty tier and code-point order expected")
	}
}
