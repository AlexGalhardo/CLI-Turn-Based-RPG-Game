package presentation_test

// M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen.

import (
	"reflect"
	"strconv"
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
)

func freshData(t *testing.T) *domain.GameData {
	t.Helper()

	data, err := infrastructure.LoadGameData(assets.Shared())
	if err != nil {
		t.Fatal(err)
	}

	return data
}

func controllerWith(t *testing.T, data *domain.GameData, dir string) *presentation.Controller {
	t.Helper()

	serviceSet := services(t, dir)
	serviceSet.Data = data
	seed := uint64(7)

	controller, err := presentation.NewController(serviceSet, &seed, "en", nil)
	if err != nil {
		t.Fatal(err)
	}

	return controller
}

// testItemsData adds deterministic test items without touching the shared files.
func testItemsData(t *testing.T) *domain.GameData {
	t.Helper()

	data := freshData(t)
	data.Items = append(data.Items,
		domain.ItemDef{ID: "test_helmet", Name: "Test Helmet", Slot: domain.SlotHelmet, Type: "helmet", Stats: []domain.StatValue{{Stat: domain.StatArmor, Value: 10}, {Stat: domain.StatMaxHp, Value: 50}}, Value: 100},
		domain.ItemDef{ID: "test_ring", Name: "Test Ring", Slot: domain.SlotRing, Type: "ring", Stats: []domain.StatValue{{Stat: domain.StatCritChance, Value: 80}, {Stat: domain.StatDodge, Value: 90}}, Value: 100},
		domain.ItemDef{ID: "test_axe", Name: "Test Axe", Slot: domain.SlotWeapon, Type: "axe", Stats: []domain.StatValue{{Stat: domain.StatAttack, Value: 20}}, Value: 100},
		domain.ItemDef{ID: "test_rod", Name: "Test Rod", Slot: domain.SlotWeapon, Type: "rod", Stats: []domain.StatValue{{Stat: domain.StatAttack, Value: 1}}, Value: 100},
	)

	return data.Index()
}

func calmData(t *testing.T) *domain.GameData {
	t.Helper()

	data := freshData(t)
	for i := range data.Balance.EnemyClasses {
		row := &data.Balance.EnemyClasses[i]
		row.Dodge, row.Parry, row.Crit, row.Heal = 0, 0, 0, 0
	}

	data.Balance.EliteChancePct = 0

	return data
}

func labels(controller *presentation.Controller) []string {
	result := []string{}
	for _, option := range controller.Options() {
		result = append(result, option.Label)
	}

	return result
}

func newItem(uid int, itemID, rarity string, tier int, affixes ...domain.AffixRoll) domain.ItemInstance {
	if affixes == nil {
		affixes = []domain.AffixRoll{}
	}

	return domain.ItemInstance{UID: uid, ItemID: itemID, Rarity: rarity, Tier: tier, Affixes: affixes}
}

func TestController_SettingsToggleAndPersist(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	controller := newController(t, dir, "en")
	controller.Press("6")

	if got := labels(controller); got[1] != "Auto-equip on new runs: Off" || got[2] != "Auto-battle speed: 1x" {
		t.Fatalf("settings labels = %v", got)
	}

	press(controller, "2", "3")

	want := infrastructure.Settings{AutoEquip: true, BattleSpeed: 2}
	if controller.Settings != want || controller.AutoBattleIntervalMs() != presentation.AutoBattleBaseMs/2 {
		t.Fatalf("settings = %+v", controller.Settings)
	}

	if saved, err := controller.Services.Settings.Load(); err != nil || saved != want {
		t.Fatalf("saved = %+v", saved)
	}

	controller.Press("3")

	if controller.Settings.BattleSpeed != 1 {
		t.Fatal("the battle speed cycles")
	}

	press(controller, "1", "1")

	if controller.View != presentation.ViewSettings {
		t.Fatal("the language screen returns to the settings")
	}

	if saved, _ := controller.Services.Settings.Load(); saved != (infrastructure.Settings{Locale: "en", AutoEquip: true, BattleSpeed: 1}) {
		t.Fatalf("saved = %+v", saved)
	}
}

func TestController_NewRunAutoEquipStepMarksTheDefault(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	if err := infrastructure.NewSettingsRepository(dir).Save(infrastructure.Settings{Locale: "en", AutoEquip: true, BattleSpeed: 1}); err != nil {
		t.Fatal(err)
	}

	controller := newController(t, dir, "en")
	press(controller, "2", "2", "Z", "e", "d", "enter", "1", "0")

	if controller.View != presentation.ViewVocation {
		t.Fatal("back from the auto-equip step")
	}

	controller.Press("1")

	if controller.View != presentation.ViewAutoEquip || controller.Title() != controller.T("new_run.auto_equip", nil) {
		t.Fatal("auto-equip step expected")
	}

	if got := labels(controller); !strings.HasSuffix(got[0], "(default)") || strings.HasSuffix(got[1], "(default)") {
		t.Fatalf("labels = %v", got)
	}

	controller.Press("1")

	if controller.View != presentation.ViewMerchant || !controller.Session.State().Config.AutoEquip {
		t.Fatal("the run starts with auto-equip")
	}
}

func battleController(t *testing.T) *presentation.Controller {
	t.Helper()

	controller := newController(t, t.TempDir(), "en")
	startRun(controller, "Zed", "1")
	controller.Press("0")

	if controller.View != presentation.ViewBattle {
		t.Fatal("battle expected")
	}

	return controller
}

func TestController_AutoBattleMenuAndInstantRun(t *testing.T) {
	t.Parallel()

	controller := battleController(t)
	controller.Press("5")

	if controller.View != presentation.ViewAutoBattle || !reflect.DeepEqual(optionKeys(controller), []string{"1", "2", "3", "0"}) {
		t.Fatal("auto-battle menu")
	}

	controller.Press("0")

	if controller.View != presentation.ViewBattle {
		t.Fatal("back to the battle")
	}

	press(controller, "5", "3")

	started := controller.T("auto_battle.started", map[string]any{"mode": controller.T("auto_battle.balanced", nil)})
	if !controller.AutoBattleActive() || controller.Log[len(controller.Log)-1] != started {
		t.Fatal("the auto-battle starts")
	}

	state := controller.Session.State()
	turn := state.Turn

	controller.Press("1")

	if state.Turn != turn {
		t.Fatal("keys are ignored during the auto-battle")
	}

	controller.RunAutoBattle()

	if controller.AutoBattleActive() || state.Phase == domain.PhaseBattle ||
		(controller.View != presentation.ViewMerchant && controller.View != presentation.ViewGameOver) {
		t.Fatal("the fight is played to the end")
	}

	if controller.AutoBattleStep() {
		t.Fatal("no auto-battle outside a fight")
	}
}

func TestController_AutoBattleStepsOneTurnAtATime(t *testing.T) {
	t.Parallel()

	controller := battleController(t)
	state := controller.Session.State()
	state.Monster.HP, state.Monster.MaxHP = 1_000_000, 1_000_000

	press(controller, "5", "1")

	turn := state.Turn
	state.Player.HP = 1_000_000

	if !controller.AutoBattleStep() || state.Turn != turn+1 {
		t.Fatal("one turn per step")
	}
}

func victoryController(t *testing.T, dir string) *presentation.Controller {
	t.Helper()

	data := calmData(t)
	controller := controllerWith(t, data, dir)
	startRun(controller, "Zed", "1")
	controller.Session.State().Round = data.Balance.FinalRound - 1
	controller.Press("0")

	for range 50 {
		monster := controller.Session.State().Monster
		if monster == nil {
			break
		}

		monster.HP = 1

		controller.Press("1")
	}

	if controller.View != presentation.ViewVictory {
		t.Fatalf("victory expected, got %s", controller.View)
	}

	return controller
}

func TestController_VictoryScreenEndRun(t *testing.T) {
	t.Parallel()

	controller := victoryController(t, t.TempDir())

	if controller.Title() != controller.T("victory.title", nil) || !strings.Contains(controller.BodyLines()[0], "Ferumbras") {
		t.Fatal("victory screen")
	}

	controller.Press("9")

	if controller.View != presentation.ViewVictory {
		t.Fatal("unknown keys do nothing")
	}

	controller.Press("1")

	if controller.View != presentation.ViewGameOver || controller.Title() != controller.T("gameover.title_won", nil) ||
		!strings.Contains(controller.BodyLines()[0], "won the run") {
		t.Fatalf("won game over: %v", controller.BodyLines())
	}

	press(controller, "2", "3")

	if !strings.Contains(controller.BodyLines()[0], "WON") {
		t.Fatal("the Hall of Fame marks won runs")
	}
}

func TestController_VictoryScreenContinue(t *testing.T) {
	t.Parallel()

	controller := victoryController(t, t.TempDir())
	controller.Press("2")

	if controller.View != presentation.ViewMerchant || !controller.Session.State().Won {
		t.Fatal("continue enters the merchant")
	}
}

func TestController_ContinueSavedVictoryReturnsToTheVictoryScreen(t *testing.T) {
	t.Parallel()

	controller := victoryController(t, t.TempDir())
	controller.Session = nil
	controller.View = presentation.ViewTitle
	controller.Press("1")

	if controller.View != presentation.ViewVictory {
		t.Fatal("a saved victory resumes on the victory screen")
	}
}

func equipmentController(t *testing.T) *presentation.Controller {
	t.Helper()

	controller := controllerWith(t, testItemsData(t), t.TempDir())
	startRun(controller, "Zed", "1")
	controller.Press("3")

	if controller.View != presentation.ViewEquipment {
		t.Fatal("equipment screen expected")
	}

	return controller
}

func TestController_EquipmentScreenListsEverySlotAndTheBag(t *testing.T) {
	t.Parallel()

	controller := equipmentController(t)
	data := controller.Services.Data
	player := controller.Session.State().Player
	player.Bag = append(player.Bag, newItem(900, "test_axe", "rare", 0), newItem(901, "test_rod", "common", 0), newItem(902, "test_helmet", "legendary", 9))
	lines := controller.BodyLines()
	colors := controller.BodyColors()
	starter := player.Equipment[domain.SlotWeapon]

	if lines[0] != "EQUIPPED · total score "+strconv.Itoa(domain.ItemScore(starter, data)) || !strings.HasPrefix(lines[1], "Weapon: Sword [Common] · Lv 1") {
		t.Fatalf("header lines = %v", lines[:2])
	}

	if lines[2] != "Shield: - empty -" || colors[2] != presentation.StyleWarning || lines[len(lines)-1] != "BAG (usable)" {
		t.Fatalf("lines = %v", lines)
	}

	empty := 0

	for _, line := range lines {
		if strings.Contains(line, "- empty -") {
			empty++
		}
	}

	if empty != 7 {
		t.Fatalf("empty slots = %d", empty)
	}

	options := controller.Options()
	if !reflect.DeepEqual(optionKeys(controller), []string{"1", "2", "3", "0"}) {
		t.Fatalf("keys = %v", optionKeys(controller))
	}

	axe, helmet, slot := options[0], options[1], options[2]
	delta := domain.ItemScore(newItem(900, "test_axe", "rare", 0), data) - domain.ItemScore(starter, data)

	if axe.Detail != presentation.FormatDelta(delta) || axe.DetailColor != presentation.StyleGain || axe.Color != "rare" {
		t.Fatalf("axe option = %+v", axe)
	}

	if helmet.Color != presentation.StyleDim || !strings.HasSuffix(helmet.Label, "requires Lv 37") || slot.Label != "Weapon: Sword [Common]" {
		t.Fatalf("options = %+v", options)
	}
}

func TestController_ComparisonShowsStatAndScoreDeltas(t *testing.T) {
	t.Parallel()

	controller := equipmentController(t)
	player := controller.Session.State().Player
	player.Equipment[domain.SlotHelmet] = newItem(800, "test_helmet", "common", 0, domain.AffixRoll{Stat: domain.StatDodge, Value: 3})
	player.Bag = append(player.Bag, newItem(801, "test_helmet", "rare", 0, domain.AffixRoll{Stat: domain.StatCritChance, Value: 2}))

	controller.Press("1")

	if controller.View != presentation.ViewCompare || controller.Title() != "Helmet: Test Helmet → Test Helmet" {
		t.Fatalf("compare title = %q", controller.Title())
	}

	lines := controller.BodyLines()
	colors := map[string]string{}

	for index, color := range controller.BodyColors() {
		colors[lines[index]] = color
	}

	expected := map[string]string{
		"Armor: 10 → 15 (+5)":                presentation.StyleGain,
		"Max HP: 50 → 75 (+25)":              presentation.StyleGain,
		"Critical chance: 0 → 2 (+2)":        presentation.StyleGain,
		"Dodge: 3 → 0 (-3)":                  presentation.StyleLoss,
		"Affixes gained: +2 Critical chance": presentation.StyleGain,
		"Affixes lost: +3 Dodge":             presentation.StyleLoss,
	}
	for line, color := range expected {
		if got, ok := colors[line]; !ok || got != color {
			t.Fatalf("%q: %q (lines %v)", line, got, lines)
		}
	}

	if !strings.HasPrefix(lines[len(lines)-1], "Score: ") || !reflect.DeepEqual(optionKeys(controller), []string{"1", "0"}) {
		t.Fatalf("lines = %v", lines)
	}

	controller.Press("1")

	if controller.View != presentation.ViewEquipment || player.Equipment[domain.SlotHelmet].UID != 801 {
		t.Fatal("equip from the comparison")
	}
}

func TestController_ComparisonWarnsAboutTheRequiredLevel(t *testing.T) {
	t.Parallel()

	controller := equipmentController(t)
	state := controller.Session.State()
	state.Player.Bag = append(state.Player.Bag, newItem(810, "test_axe", "common", 3))

	controller.Press("1")

	lines, colors := controller.BodyLines(), controller.BodyColors()
	if colors[len(colors)-1] != presentation.StyleLoss || lines[len(lines)-1] != "Requires level 13 (you are level 1)." {
		t.Fatalf("lines = %v", lines)
	}

	controller.Press("1")

	if controller.Message != controller.T("error.level_too_low", nil) || controller.View != presentation.ViewEquipment {
		t.Fatal("equipping above the level fails")
	}

	press(controller, "0", "3")

	for _, option := range controller.Options() {
		if strings.HasPrefix(option.Label, "Weapon") {
			controller.Press(option.Key)

			break
		}
	}

	if controller.View != presentation.ViewEquippedSlot || controller.Title() != "Weapon" || controller.BodyLines()[1] != "Attack: 6" {
		t.Fatalf("slot view: %s %v", controller.Title(), controller.BodyLines())
	}

	controller.Press("1")

	if _, equipped := state.Player.Equipment[domain.SlotWeapon]; controller.View != presentation.ViewEquipment || equipped {
		t.Fatal("unequip from the slot view")
	}
}

func TestController_CompareAndSlotViewsSurviveMissingItems(t *testing.T) {
	t.Parallel()

	controller := equipmentController(t)

	if lines := controller.BodyLines(); lines[len(lines)-1] != controller.T("equipment.bag_empty", nil) {
		t.Fatal("empty bag line")
	}

	controller.View = presentation.ViewCompare

	if len(controller.BodyLines()) != 0 || controller.Title() != controller.T("merchant.equipment", nil) {
		t.Fatal("comparison without an item")
	}

	controller.View = presentation.ViewEquippedSlot
	controller.Press("0")
	controller.View = presentation.ViewEquippedSlot
	controller.Session.State().Player.Equipment = map[domain.Slot]domain.ItemInstance{}

	if lines := controller.BodyLines(); !reflect.DeepEqual(lines, []string{controller.T("equipment.empty", nil)}) {
		t.Fatalf("lines = %v", lines)
	}
}

func TestController_MonsterViewAndHallOfFameMarkers(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	data := freshData(t)
	data.Balance.EliteChancePct = 100
	controller := controllerWith(t, data, dir)
	repository := controller.Services.Repositories.Profile

	profile, err := repository.Load()
	if err != nil {
		t.Fatal(err)
	}

	profile.HallOfFame = append(profile.HallOfFame, application.HallOfFameEntry{
		RunID: "r", Name: "Ana", Vocation: "mage", Difficulty: "hard", Round: 100, Level: 50, EndedAt: "2026-01-01T00:00:00Z", Won: true,
	})

	if err := repository.Save(profile); err != nil {
		t.Fatal(err)
	}

	controller.Press("3")

	if !strings.Contains(controller.BodyLines()[0], "WON") {
		t.Fatal("won marker")
	}

	controller.Press("0")
	startRun(controller, "Zed", "1")
	controller.Press("0")

	monster := controller.MonsterView()
	if monster == nil || monster.EnemyClass != "elite" {
		t.Fatal("elite monster view")
	}

	if !strings.Contains(strings.Join(controller.Log, "\n"), "ELITE") {
		t.Fatal("the log announces the elite")
	}
}
