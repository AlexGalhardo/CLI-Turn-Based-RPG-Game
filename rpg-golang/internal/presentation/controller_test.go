package presentation_test

import (
	"slices"
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
)

func startRun(controller *presentation.Controller, name, vocationKey string) {
	press(controller, "2", "2")

	for _, character := range name {
		controller.Press(string(character))
	}

	press(controller, "enter", vocationKey)
}

func optionKeys(controller *presentation.Controller) []string {
	keys := []string{}
	for _, option := range controller.Options() {
		keys = append(keys, option.Key)
	}

	return keys
}

func TestNewController(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()

	controller, err := presentation.NewController(services(t, dir), nil, "", func() uint64 { return 5 })
	if err != nil || controller.View != presentation.ViewLanguage {
		t.Fatal("first launch asks for the language")
	}

	controller.Press("2")

	if controller.View != presentation.ViewTitle || !slices.ContainsFunc(controller.Options(), func(o presentation.MenuOption) bool { return o.Label == "Sair" }) {
		t.Fatal("portuguese title menu expected")
	}

	if settings, _ := infrastructure.NewSettingsRepository(dir).Load(); settings.Locale != "pt-BR" {
		t.Fatal("the language is saved")
	}

	press(controller, "6", "1")

	if controller.Locale != "en" {
		t.Fatal("language can be changed from the title")
	}

	startRun(controller, "Seedy", "1")

	if controller.Session == nil || controller.Session.State().Seed != 5 {
		t.Fatal("the seed source is used when no seed is given")
	}
}

func TestController_NameInput(t *testing.T) {
	t.Parallel()

	controller := newController(t, t.TempDir(), "en")
	press(controller, "2", "1")

	if controller.View != presentation.ViewName {
		t.Fatal("difficulty leads to name input")
	}

	controller.Press("enter")

	if controller.Message != controller.T("new_run.name_invalid", nil) {
		t.Fatal("empty names are refused")
	}

	for _, character := range "Abcdefghijklmnopqrstuvwxyz" {
		controller.Press(string(character))
	}

	controller.Press("backspace")

	if controller.InputPrompt() != "> Abcdefghijklmno_" {
		t.Fatalf("prompt = %q", controller.InputPrompt())
	}

	press(controller, "escape", "0")

	if controller.View != presentation.ViewTitle || slices.Contains(optionKeys(controller), "1") {
		t.Fatal("back to a title without continue")
	}

	controller.Press("0")

	if !controller.ExitRequested {
		t.Fatal("quit requests exit")
	}
}

func TestController_Merchant(t *testing.T) {
	t.Parallel()

	controller := newController(t, t.TempDir(), "en")
	startRun(controller, "Zed", "1")
	player := controller.Session.State().Player

	if controller.Title() != controller.T("merchant.title_start", nil) || !strings.Contains(controller.Header(), "Seed 7") {
		t.Fatal("merchant start title and header")
	}

	controller.Press("2")

	if lines := controller.BodyLines(); len(lines) != 1 || lines[0] != controller.T("merchant.empty_bag", nil) {
		t.Fatal("empty bag message")
	}

	controller.Press("0")

	player.Bag = append(player.Bag,
		domain.ItemInstance{UID: 900, ItemID: "hand_axe", Rarity: "rare"},
		domain.ItemInstance{UID: 901, ItemID: "bow", Rarity: "common"},
	)

	controller.Press("3")

	labels := strings.Join(func() []string {
		result := []string{}
		for _, option := range controller.Options() {
			result = append(result, option.Label)
		}

		return result
	}(), "|")
	if !strings.Contains(labels, "Hand Axe") || strings.Contains(labels, "Equip Bow") {
		t.Fatalf("equipment menu = %s", labels)
	}

	press(controller, "1", "0", "2", optionKeys(controller)[0], "0", "4")

	if len(controller.Options()) != len(controller.Session.State().MerchantStock)+1 {
		t.Fatal("stock menu lists the stock")
	}

	player.Gold = 0

	controller.Press("1")

	if controller.Message != controller.T("error.not_enough_gold", nil) {
		t.Fatal("errors become messages")
	}

	press(controller, "0", "1", "1", "x", "enter", "1", "escape", "0", "5")

	if !slices.ContainsFunc(controller.BodyLines(), func(line string) bool { return strings.Contains(line, "Equipment") }) {
		t.Fatal("character sheet shows equipment")
	}

	press(controller, "0", "0")

	if controller.View != presentation.ViewBattle || controller.Title() != "Your turn" || controller.MonsterView() == nil || controller.PlayerView() == nil {
		t.Fatal("next fight opens the battle")
	}
}

func TestController_Battle(t *testing.T) {
	t.Parallel()

	controller := newController(t, t.TempDir(), "en")
	startRun(controller, "Mia", "3")
	controller.Press("0")

	state := controller.Session.State()
	state.Player.Potions = map[string]int{}

	controller.Press("3")

	if lines := controller.BodyLines(); len(lines) != 1 || lines[0] != controller.T("battle.no_potions", nil) {
		t.Fatal("no potions message")
	}

	controller.Press("0")

	state.Player.MP = 0

	press(controller, "2", presentation.ListKey(0))

	if controller.Message != controller.T("error.not_enough_mana", nil) {
		t.Fatal("not enough mana message")
	}

	press(controller, "escape", "4", "q")

	if controller.View != presentation.ViewTitle || controller.Session != nil {
		t.Fatal("save & quit returns to the title")
	}

	controller.Press("1")

	if controller.View != presentation.ViewMerchant || controller.Session.Info.Sessions != 2 {
		t.Fatal("continue resumes the run")
	}
}

func TestController_InformativeScreens(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	repository := infrastructure.NewFileProfileRepository(dir)
	profile := application.NewProfile()
	profile.Bestiary["rat"] = &application.BestiaryEntry{Kills: 9, FirstKilledAt: "2026-01-01T00:00:00Z"}
	profile.Bestiary["bat"] = &application.BestiaryEntry{Kills: 1, FirstKilledAt: "2026-01-01T00:00:00Z"}

	if err := repository.Save(profile); err != nil {
		t.Fatal(err)
	}

	controller := newController(t, dir, "en")
	controller.Press("4")
	lines := controller.BodyLines()

	if len(lines) != presentation.PageSize+2 ||
		!slices.ContainsFunc(lines, func(l string) bool { return strings.HasPrefix(l, "Rat") && strings.Contains(l, "weak") }) ||
		!slices.ContainsFunc(lines, func(l string) bool { return strings.HasPrefix(l, "Bat") && !strings.Contains(l, "weak") }) {
		t.Fatalf("bestiary page = %v", lines)
	}

	for range 50 {
		controller.Press("n")
	}

	if last := controller.BodyLines(); !strings.HasPrefix(last[len(last)-1], "Page 12/12") {
		t.Fatalf("last page = %v", last)
	}

	press(controller, "p", "0", "3")

	if lines := controller.BodyLines(); len(lines) != 1 || lines[0] != controller.T("hall.empty", nil) {
		t.Fatal("empty hall of fame")
	}

	press(controller, "0", "5")

	for _, line := range controller.BodyLines()[:presentation.PageSize] {
		if !strings.HasPrefix(line, "[ ]") {
			t.Fatalf("achievements start locked: %s", line)
		}
	}
}
