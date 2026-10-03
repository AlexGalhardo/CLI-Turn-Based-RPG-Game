package tui_test

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	tea "charm.land/bubbletea/v2"
	"github.com/charmbracelet/x/ansi"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation/tui"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/version"
)

// dataDir replaces t.TempDir: on Windows, antivirus/indexing can briefly lock the save files a whole run writes,
// making t.TempDir's cleanup fail with "directory not empty", so removal is retried for a moment.
func dataDir(t *testing.T) string {
	t.Helper()

	dir, err := os.MkdirTemp("", "rpg-tui-") //nolint:usetesting // t.TempDir cannot retry its cleanup.
	if err != nil {
		t.Fatal(err)
	}

	t.Cleanup(func() {
		for range 20 {
			if os.RemoveAll(dir) == nil {
				return
			}

			time.Sleep(50 * time.Millisecond)
		}
	})

	return dir
}

// harness drives the real Bubble Tea model with key messages (Elm architecture: Update/View are pure).
type harness struct {
	t     *testing.T
	model tea.Model
}

func newHarness(t *testing.T, dir, locale string) *harness {
	t.Helper()

	return newAnimatedHarness(t, dir, locale, false)
}

func newAnimatedHarness(t *testing.T, dir, locale string, animate bool) *harness {
	t.Helper()

	data, err := infrastructure.LoadGameData(assets.Shared())
	if err != nil {
		t.Fatal(err)
	}

	services := presentation.Services{
		Data: data, Shared: assets.Shared(), Settings: infrastructure.NewSettingsRepository(dir),
		Repositories: application.Repositories{
			Saves: infrastructure.NewFileSaveRepository(dir), History: infrastructure.NewFileHistoryRepository(dir),
			Profile: infrastructure.NewFileProfileRepository(dir),
		},
		Clock: infrastructure.SystemClock{}, Version: version.Version,
	}
	seed := uint64(42)

	controller, err := presentation.NewController(services, &seed, locale, nil)
	if err != nil {
		t.Fatal(err)
	}

	model, _ := tui.NewModel(controller, infrastructure.NewArtLibrary(assets.Shared()), animate).
		Update(tea.WindowSizeMsg{Width: presentation.MinColumns, Height: presentation.MinRows})

	return &harness{t: t, model: model}
}

func (h *harness) controller() *presentation.Controller {
	model, ok := h.model.(tui.Model)
	if !ok {
		h.t.Fatal("unexpected model type")
	}

	return model.Controller
}

// press sends keys; it returns true when the program asked to quit.
func (h *harness) press(keys ...string) bool {
	quit := false

	for _, key := range keys {
		var msg tea.KeyPressMsg

		switch key {
		case "enter":
			msg = tea.KeyPressMsg{Code: tea.KeyEnter}
		case "escape":
			msg = tea.KeyPressMsg{Code: tea.KeyEscape}
		case "backspace":
			msg = tea.KeyPressMsg{Code: tea.KeyBackspace}
		default:
			msg = tea.KeyPressMsg{Code: []rune(key)[0], Text: key}
		}

		var cmd tea.Cmd

		h.model, cmd = h.model.Update(msg)
		if cmd != nil {
			if _, ok := cmd().(tea.QuitMsg); ok {
				quit = true
			}
		}
	}

	return quit
}

func (h *harness) screen() string {
	return ansi.Strip(h.model.View().Content)
}

func (h *harness) typeText(text string) {
	for _, character := range text {
		h.press(string(character))
	}
}

func TestModel_FirstLaunch(t *testing.T) {
	t.Parallel()

	dir := dataDir(t)
	h := newHarness(t, dir, "")

	if h.controller().View != presentation.ViewLanguage {
		t.Fatal("first launch asks the language")
	}

	h.press("2")

	if !strings.Contains(h.screen(), "Nova jornada") {
		t.Fatal("portuguese title screen")
	}

	h.press("2", "3")
	h.typeText("Ana")
	h.press("enter", "3")

	if h.controller().View != presentation.ViewAutoEquip {
		t.Fatal("the vocation leads to the auto-equip choice")
	}

	h.press("2")

	if screen := h.screen(); !strings.Contains(screen, "Ana") || !strings.Contains(screen, "Mago") {
		t.Fatalf("merchant screen:\n%s", screen)
	}

	if _, err := os.Stat(filepath.Join(dir, "save.json")); err != nil {
		t.Fatal("the run is saved at the merchant")
	}
}

func TestModel_BattleAndContinue(t *testing.T) {
	t.Parallel()

	h := newHarness(t, dataDir(t), "en")
	h.press("2", "2")
	h.typeText("Bo")
	h.press("enter", "1", "2", "1", "1", "1", "enter")

	if h.controller().Session.State().Player.PotionCount("health_potion") != 6 {
		t.Fatal("potion purchase through the ui")
	}

	h.press("0", "5")

	if !strings.Contains(h.screen(), "Equipment") {
		t.Fatal("character sheet")
	}

	h.press("0", "0")

	if screen := h.screen(); !strings.Contains(screen, "Round 1") || !strings.Contains(screen, "HP") || !strings.Contains(screen, "[1] Attack") {
		t.Fatalf("battle screen:\n%s", screen)
	}

	monster := h.controller().Session.State().Monster
	monster.HP, monster.MaxHP = 1_000_000, 1_000_000

	h.press("1", "2", presentation.ListKey(0), "3", "escape", "q")

	if !strings.Contains(h.screen(), "Continue") {
		t.Fatal("save & quit shows continue")
	}

	h.press("1")

	if h.controller().View != presentation.ViewMerchant || h.controller().Session.Info.Sessions != 2 {
		t.Fatal("continue resumes")
	}
}

func keysFor(command application.Command, controller *presentation.Controller) []string {
	data := controller.Services.Data
	state := controller.Session.State()

	switch command.Type {
	case application.CmdAttack:
		return []string{"1"}
	case application.CmdDefend:
		return []string{"4"}
	case application.CmdCast:
		return []string{"2", presentation.ListKey(slices.Index(data.Vocation(state.Player.VocationID).Spells, command.SpellID))}
	case application.CmdPotion:
		owned := []string{}

		for _, potion := range data.Potions {
			if state.Player.PotionCount(potion.ID) > 0 {
				owned = append(owned, potion.ID)
			}
		}

		return []string{"3", presentation.ListKey(slices.Index(owned, command.PotionID))}
	case application.CmdNextFight:
		return []string{"0"}
	case application.CmdBuyPotion:
		index := slices.Index(application.AvailablePotions(state, data), command.PotionID)
		keys := []string{"1", presentation.ListKey(index)}

		for _, digit := range itoa(command.Quantity) {
			keys = append(keys, string(digit))
		}

		return append(keys, "enter", "0")
	case application.CmdSellItem:
		return []string{"2", presentation.ListKey(slices.IndexFunc(state.Player.Bag, func(i domain.ItemInstance) bool { return i.UID == command.UID })), "0"}
	case application.CmdEquip:
		vocation := data.Vocation(state.Player.VocationID)
		usable := []int{}

		for _, item := range state.Player.Bag {
			if application.CanUse(data.Item(item.ItemID), vocation) {
				usable = append(usable, item.UID)
			}
		}

		return []string{"3", presentation.ListKey(slices.Index(usable, command.UID)), "1", "0"}
	case application.CmdEndRun:
		return []string{"1"}
	default:
		return []string{"4", presentation.ListKey(command.Index), "0"}
	}
}

func itoa(value int) string {
	digits := ""
	for value > 0 {
		digits = string(rune('0'+value%10)) + digits
		value /= 10
	}

	return digits
}

func TestModel_WholeRunByKeys(t *testing.T) {
	t.Parallel()

	dir := dataDir(t)
	h := newHarness(t, dir, "en")
	h.press("2", "3")
	h.typeText("Hero")
	h.press("enter", "1", "2")

	controller := h.controller()
	bot := application.NewGreedyBot(controller.Services.Data)
	jumped := false

	for range 5000 {
		state := controller.Session.State()
		if state.Phase == domain.PhaseGameOver {
			break
		}

		if !jumped && state.Phase == domain.PhaseMerchant && state.Stats.TotalKills() > 0 {
			// After the first kill, skip ahead so the run ends quickly (each fight costs many key presses).
			state.Round = 95
			jumped = true
		}

		h.press(keysFor(bot.Choose(state), controller)...)
	}

	expected := "GAME OVER"
	if controller.Session.State().Won {
		expected = "RUN COMPLETE"
	}

	if controller.View != presentation.ViewGameOver || !strings.Contains(h.screen(), expected) {
		t.Fatalf("%s screen expected:\n%s", expected, h.screen())
	}

	h.press("2", "3")

	if !strings.Contains(h.screen(), "Hero") {
		t.Fatal("hall of fame lists the run")
	}

	h.press("0", "5")

	if !strings.Contains(h.screen(), "[x] First Blood") {
		t.Fatal("achievement unlocked")
	}

	h.press("0", "4", "n", "p", "0")

	if !h.press("0") {
		t.Fatal("quit from the title ends the program")
	}

	if entries, err := os.ReadDir(filepath.Join(dir, "history")); err != nil || len(entries) != 1 {
		t.Fatal("one finished run in history")
	}
}

func TestModel_FullRunWithAutoBattle(t *testing.T) {
	t.Parallel()

	dir := dataDir(t)
	h := newHarness(t, dir, "en")
	h.press("2", "1")
	h.typeText("Auto")
	h.press("enter", "2", "1")

	controller := h.controller()
	session := controller.Session

	if !session.State().Config.AutoEquip {
		t.Fatal("auto-equip chosen at the new-run step")
	}

	modes := []string{"1", "2", "3"}

	for fight := range 5000 {
		state := session.State()
		if state.Phase == domain.PhaseGameOver {
			break
		}

		if state.Phase == domain.PhaseVictory {
			if !strings.Contains(h.screen(), "VICTORY") {
				t.Fatalf("victory screen expected:\n%s", h.screen())
			}

			h.press("1")

			continue
		}

		h.press("0", "5", modes[fight%len(modes)])

		if controller.AutoBattleActive() {
			t.Fatal("without animation the auto-battle is instant")
		}
	}

	if controller.View != presentation.ViewGameOver || session.State().Round < 1 {
		t.Fatal("the run ends with auto-battles")
	}

	if entries, err := os.ReadDir(filepath.Join(dir, "history")); err != nil || len(entries) != 1 {
		t.Fatal("one finished run in history")
	}
}

func TestModel_AutoBattleIsPacedByATimer(t *testing.T) {
	t.Parallel()

	dir := dataDir(t)
	if err := infrastructure.NewSettingsRepository(dir).Save(infrastructure.Settings{Locale: "en", BattleSpeed: 2}); err != nil {
		t.Fatal(err)
	}

	h := newAnimatedHarness(t, dir, "", true)
	controller := h.controller()
	send := func(msg tea.Msg) tea.Cmd {
		var cmd tea.Cmd

		h.model, cmd = h.model.Update(msg)

		return cmd
	}
	key := func(text string) tea.Cmd {
		if text == "enter" {
			return send(tea.KeyPressMsg{Code: tea.KeyEnter})
		}

		return send(tea.KeyPressMsg{Code: []rune(text)[0], Text: text})
	}

	for _, k := range []string{"2", "1", "T", "i", "m", "enter", "1", "2", "0"} {
		key(k)
	}

	if controller.View != presentation.ViewBattle {
		t.Fatal("battle expected")
	}

	state := controller.Session.State()
	state.Monster.HP, state.Monster.MaxHP = 1_000_000, 1_000_000
	state.Player.HP = 1_000_000

	key("5")

	tick := key("1")
	if !controller.AutoBattleActive() || tick == nil || controller.AutoBattleIntervalMs() != 300 {
		t.Fatal("the auto-battle starts a 300 ms timer at 2x")
	}

	turn := state.Turn
	started := time.Now()
	msg := tick()

	if time.Since(started) < 250*time.Millisecond {
		t.Fatal("the timer waits for the battle speed interval")
	}

	tick = send(msg)
	if state.Turn <= turn || tick == nil {
		t.Fatal("each tick plays one turn and schedules the next")
	}

	turn = state.Turn
	if key("4"); state.Turn != turn {
		t.Fatal("keys are ignored while the auto-battle runs")
	}

	state.Monster.HP = 1

	for range 20 {
		if tick == nil {
			break
		}

		tick = send(tick())
	}

	if controller.AutoBattleActive() || controller.View == presentation.ViewBattle {
		t.Fatal("the auto-battle stops when the fight ends")
	}
}

func TestModel_SmallTerminal(t *testing.T) {
	t.Parallel()

	h := newHarness(t, dataDir(t), "en")
	h.model, _ = h.model.Update(tea.WindowSizeMsg{Width: 60, Height: 20})

	if !strings.Contains(h.screen(), "resize") {
		t.Fatal("small terminals get a resize message")
	}

	if !h.press("ctrl+c") && h.model.Init() != nil {
		t.Fatal("no animation timer without animations")
	}
}

func TestKeyName(t *testing.T) {
	t.Parallel()

	tests := []struct {
		msg  tea.KeyPressMsg
		want string
	}{
		{tea.KeyPressMsg{Code: tea.KeyEnter}, "enter"},
		{tea.KeyPressMsg{Code: tea.KeyEscape}, "escape"},
		{tea.KeyPressMsg{Code: tea.KeyBackspace}, "backspace"},
		{tea.KeyPressMsg{Code: 'a', Text: "a"}, "a"},
	}
	for _, tt := range tests {
		if got := tui.KeyName(tt.msg); got != tt.want {
			t.Fatalf("KeyName = %q, want %q", got, tt.want)
		}
	}
}
