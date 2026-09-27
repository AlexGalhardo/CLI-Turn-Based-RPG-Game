package presentation

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"io/fs"
	"maps"
	"slices"
	"strconv"
	"strings"
	"unicode"
	"unicode/utf8"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

// Controller limits (same values as the reference).
const (
	maxLogLines       = 50
	maxNameLength     = 16
	maxQuantityDigits = 2
	PageSize          = 10
)

// View is a UI screen.
type View string

// Screens (docs/tui.md).
const (
	ViewLanguage     View = "language"
	ViewTitle        View = "title"
	ViewDifficulty   View = "difficulty"
	ViewName         View = "name"
	ViewVocation     View = "vocation"
	ViewMerchant     View = "merchant"
	ViewBuyPotions   View = "buy_potions"
	ViewQuantity     View = "quantity"
	ViewSell         View = "sell"
	ViewEquipment    View = "equipment"
	ViewStock        View = "stock"
	ViewCharacter    View = "character"
	ViewBattle       View = "battle"
	ViewSpells       View = "spells"
	ViewPotions      View = "potions"
	ViewGameOver     View = "game_over"
	ViewHallOfFame   View = "hall_of_fame"
	ViewBestiary     View = "bestiary"
	ViewAchievements View = "achievements"
)

// IsPaged reports whether a view shows paged informative lines (and hides the combat log).
func IsPaged(view View) bool {
	return view == ViewHallOfFame || view == ViewBestiary || view == ViewAchievements || view == ViewCharacter
}

func isBattleView(view View) bool {
	return view == ViewBattle || view == ViewSpells || view == ViewPotions
}

func isTextInput(view View) bool { return view == ViewName || view == ViewQuantity }

// MenuOption is one selectable option.
type MenuOption struct {
	Key   string
	Label string
	Color string
}

type menuEntry struct {
	option MenuOption
	action func()
}

// Services are the collaborators of the controller.
type Services struct {
	Data         *domain.GameData
	Shared       fs.FS
	Settings     *infrastructure.SettingsRepository
	Repositories application.Repositories
	Clock        application.Clock
	Version      string
}

// MonsterView is what the renderer shows about the monster.
type MonsterView struct {
	Name       string
	CreatureID string
	HP         int
	MaxHP      int
	IsBoss     bool
	Element    domain.Element
	Details    string
}

// PlayerView is what the renderer shows about the player.
type PlayerView struct {
	Summary  string
	Gold     string
	HP       int
	MaxHP    int
	MP       int
	MaxMP    int
	XP       string
	Statuses string
}

// Controller is the framework-independent UI state machine (port of the reference controller).
type Controller struct {
	View          View
	Session       *application.GameSession
	Log           []string
	Message       string
	InputBuffer   string
	ExitRequested bool
	AnimationCues []string
	Page          int
	Locale        string
	Services      Services
	// Err keeps the last persistence error so the renderer can show it instead of crashing.
	Err error

	translator     *infrastructure.Translator
	formatter      *EventFormatter
	languageReturn View
	difficulty     string
	name           string
	potionID       string
	seed           *uint64
	seedSource     func() uint64
}

// RandomSeed draws a seed from the OS random source (presentation-level randomness only).
func RandomSeed() uint64 {
	var buffer [4]byte
	if _, err := rand.Read(buffer[:]); err != nil {
		return 0
	}

	return uint64(binary.LittleEndian.Uint32(buffer[:]))
}

// NewController creates the controller; localeOverride and seed may be empty/nil.
func NewController(services Services, seed *uint64, localeOverride string, seedSource func() uint64) (*Controller, error) {
	settings, err := services.Settings.Load()
	if err != nil {
		return nil, fmt.Errorf("load settings: %w", err)
	}

	if seedSource == nil {
		seedSource = RandomSeed
	}

	controller := &Controller{
		Services: services, Log: []string{}, languageReturn: ViewTitle, difficulty: "normal",
		seed: seed, seedSource: seedSource, View: ViewLanguage,
	}

	locale := infrastructure.DefaultLocale

	switch {
	case localeOverride != "":
		locale, controller.View = localeOverride, ViewTitle
	case settings.Locale != "":
		locale, controller.View = settings.Locale, ViewTitle
	}

	if err := controller.setLocale(locale); err != nil {
		return nil, err
	}

	return controller, nil
}

func (c *Controller) setLocale(locale string) error {
	translator, err := infrastructure.NewTranslator(c.Services.Shared, locale)
	if err != nil {
		return err
	}

	c.Locale, c.translator = locale, translator
	c.formatter = NewEventFormatter(c.Services.Data, translator)

	return nil
}

// T translates a key.
func (c *Controller) T(key string, params map[string]any) string { return c.translator.T(key, params) }

// ── queries used by renderers ────────────────────────────────────────────

// Title is the title of the current screen.
//
//nolint:gocyclo // one case per screen, mirroring the reference controller.
func (c *Controller) Title() string {
	switch c.View {
	case ViewLanguage:
		return c.T("language.title", nil)
	case ViewTitle:
		return c.T("app.title", nil)
	case ViewDifficulty:
		return c.T("new_run.difficulty", nil)
	case ViewName:
		return c.T("new_run.name", nil)
	case ViewVocation:
		return c.T("new_run.vocation", nil)
	case ViewMerchant:
		round := 0
		if c.Session != nil {
			round = c.Session.State().Round
		}

		if round == 0 {
			return c.T("merchant.title_start", nil)
		}

		return c.T("merchant.title", map[string]any{"round": round})
	case ViewBuyPotions:
		return c.T("merchant.buy_potions", nil)
	case ViewQuantity:
		return c.T("merchant.quantity", map[string]any{"name": c.Services.Data.Potion(c.potionID).Name})
	case ViewSell:
		return c.T("merchant.sell_items", nil)
	case ViewEquipment:
		return c.T("merchant.equipment", nil)
	case ViewStock:
		return c.T("merchant.stock", nil)
	case ViewCharacter:
		return c.T("merchant.character", nil)
	case ViewBattle:
		return c.T("battle.title", nil)
	case ViewSpells:
		return c.T("battle.spells", nil)
	case ViewPotions:
		return c.T("battle.potions", nil)
	case ViewGameOver:
		return c.T("gameover.title", nil)
	case ViewHallOfFame:
		return c.T("menu.hall_of_fame", nil)
	case ViewBestiary:
		return c.T("menu.bestiary", nil)
	case ViewAchievements:
		return c.T("menu.achievements", nil)
	default:
		return ""
	}
}

// Options lists the options of the current screen.
func (c *Controller) Options() []MenuOption {
	entries := c.menu()
	options := make([]MenuOption, len(entries))

	for i, entry := range entries {
		options[i] = entry.option
	}

	return options
}

// BodyLines are the informative lines above the options (paged for long lists).
func (c *Controller) BodyLines() []string {
	lines := c.body()
	if !IsPaged(c.View) || len(lines) <= PageSize {
		return lines
	}

	pages := (len(lines) + PageSize - 1) / PageSize
	c.Page = min(c.Page, pages-1)
	start := c.Page * PageSize
	end := min(len(lines), start+PageSize)

	return append(slices.Clone(lines[start:end]), "", c.T("menu.page", map[string]any{"page": c.Page + 1, "pages": pages}))
}

// InputPrompt returns the text input prompt, or "" when the screen has no text input.
func (c *Controller) InputPrompt() string {
	if isTextInput(c.View) {
		return "> " + c.InputBuffer + "_"
	}

	return ""
}

// Header is the top border title.
func (c *Controller) Header() string {
	if c.Session == nil {
		return c.T("app.subtitle", nil)
	}

	state := c.Session.State()
	data := c.Services.Data
	tier := domain.RoundInfoFor(max(1, state.Round), &data.Balance, data.TierCount()).Tier + 1
	difficulty := c.T("difficulty."+state.Config.DifficultyID, nil)
	roundText := c.T("hud.round", map[string]any{"round": state.Round, "tier": tier, "difficulty": difficulty})

	return roundText + " · " + c.T("hud.seed", map[string]any{"seed": state.Seed})
}

func (c *Controller) weakElements(creature *domain.MonsterDef, weak bool) []string {
	result := []string{}

	for _, element := range domain.Elements {
		resistance := creature.Resistance(element)
		if (weak && resistance > 100) || (!weak && resistance < 100) {
			result = append(result, c.T("element."+string(element), nil))
		}
	}

	return result
}

// MonsterView returns the monster panel, or nil outside battle.
func (c *Controller) MonsterView() *MonsterView {
	if c.Session == nil || c.Session.State().Monster == nil {
		return nil
	}

	monster := c.Session.State().Monster
	creature := c.Services.Data.Creature(monster.CreatureID)
	main := monster.Attacks[0]

	for _, attack := range monster.Attacks {
		if attack.Weight > main.Weight || (attack.Weight == main.Weight && attack.ID > main.ID) {
			main = attack
		}
	}

	parts := []string{}

	for _, attack := range monster.Attacks {
		label := c.T("element."+string(attack.Element), nil)
		if !slices.Contains(parts, label) {
			parts = append(parts, label)
		}
	}

	details := strings.Join(parts, " · ")
	if weak := c.weakElements(creature, true); len(weak) > 0 {
		details += " · " + c.T("hud.weak", map[string]any{"elements": strings.Join(weak, ", ")})
	}

	if statuses := c.statusText(monster.Statuses); statuses != "" {
		details += " · " + statuses
	}

	return &MonsterView{
		Name: creature.Name, CreatureID: creature.ID, HP: monster.HP, MaxHP: monster.MaxHP,
		IsBoss: monster.IsBoss, Element: main.Element, Details: details,
	}
}

func (c *Controller) statusText(statuses []domain.ActiveStatus) string {
	parts := make([]string, len(statuses))
	for i, status := range statuses {
		parts[i] = fmt.Sprintf("%s(%d)", c.T("status."+status.StatusID, nil), status.Turns)
	}

	return strings.Join(parts, " ")
}

// PlayerView returns the player panel, or nil without a run.
func (c *Controller) PlayerView() *PlayerView {
	if c.Session == nil {
		return nil
	}

	player := c.Session.State().Player
	sheet := domain.BuildSheet(player, c.Services.Data)

	return &PlayerView{
		Summary: c.T("hud.player", map[string]any{
			"name": player.Name, "vocation": c.T("vocation."+player.VocationID, nil),
			"level": player.Level, "magicLevel": player.MagicLevel,
		}),
		Gold: c.T("hud.gold", map[string]any{"gold": player.Gold}),
		HP:   player.HP, MaxHP: sheet.MaxHP, MP: player.MP, MaxMP: sheet.MaxMP,
		XP:       c.T("hud.xp", map[string]any{"xp": player.XP, "next": domain.XPForLevel(player.Level + 1)}),
		Statuses: c.statusText(player.Statuses),
	}
}

// ── input ─────────────────────────────────────────────────────────────────

// Press handles one key ("1", "a", "enter", "escape", "backspace", ...).
func (c *Controller) Press(key string) {
	c.Message = ""

	if isTextInput(c.View) {
		c.textInput(key)

		return
	}

	if IsPaged(c.View) && (key == "n" || key == "p") {
		if key == "n" {
			c.Page++
		} else {
			c.Page = max(0, c.Page-1)
		}

		return
	}

	if key == "escape" {
		key = "0"
	}

	for _, entry := range c.menu() {
		if entry.option.Key == key {
			entry.action()

			return
		}
	}
}

//nolint:gocyclo // flat key dispatch for the two text inputs, mirroring the reference controller.
func (c *Controller) textInput(key string) {
	switch {
	case key == "escape":
		c.InputBuffer = ""
		if c.View == ViewName {
			c.View = ViewDifficulty
		} else {
			c.View = ViewBuyPotions
		}
	case key == "backspace":
		if runes := []rune(c.InputBuffer); len(runes) > 0 {
			c.InputBuffer = string(runes[:len(runes)-1])
		}
	case key == "enter":
		c.submitText()
	case c.View == ViewName && utf8.RuneCountInString(key) == 1 && unicode.IsPrint([]rune(key)[0]):
		if utf8.RuneCountInString(c.InputBuffer) < maxNameLength {
			c.InputBuffer += key
		}
	case c.View == ViewQuantity && len(key) == 1 && key[0] >= '0' && key[0] <= '9' && len(c.InputBuffer) < maxQuantityDigits:
		c.InputBuffer += key
	}
}

func (c *Controller) submitText() {
	text := strings.TrimSpace(c.InputBuffer)
	c.InputBuffer = ""

	if c.View == ViewName {
		length := utf8.RuneCountInString(text)
		if length < 1 || length > maxNameLength {
			c.Message = c.T("new_run.name_invalid", nil)

			return
		}

		c.name, c.View = text, ViewVocation

		return
	}

	c.View = ViewBuyPotions

	if quantity, err := strconv.Atoi(text); err == nil && quantity > 0 {
		c.step(application.BuyPotion(c.potionID, quantity))
	}
}

// ── menus ─────────────────────────────────────────────────────────────────

//nolint:gocyclo // one case per screen, mirroring the reference controller.
func (c *Controller) menu() []menuEntry {
	data := c.Services.Data

	switch c.View {
	case ViewLanguage:
		entries := []menuEntry{}
		for i, locale := range infrastructure.SupportedLocales {
			entries = append(entries, menuEntry{MenuOption{Key: strconv.Itoa(i + 1), Label: c.T("language."+locale, nil)}, func() { c.chooseLanguage(locale) }})
		}

		return entries
	case ViewTitle:
		return c.titleMenu()
	case ViewDifficulty:
		entries := []menuEntry{}

		for i, difficulty := range data.Balance.Difficulties {
			label := c.T("difficulty."+difficulty.ID, nil) + " — " + c.T("difficulty."+difficulty.ID+".description", nil)
			entries = append(entries, menuEntry{MenuOption{Key: strconv.Itoa(i + 1), Label: label}, func() { c.chooseDifficulty(difficulty.ID) }})
		}

		return append(entries, c.back(ViewTitle))
	case ViewVocation:
		entries := []menuEntry{}

		for i, vocation := range data.Vocations {
			label := c.T("vocation."+vocation.ID, nil) + " — " + c.T("vocation."+vocation.ID+".description", nil)
			entries = append(entries, menuEntry{MenuOption{Key: strconv.Itoa(i + 1), Label: label}, func() { c.chooseVocation(vocation.ID) }})
		}

		return append(entries, c.back(ViewDifficulty))
	case ViewMerchant:
		return []menuEntry{
			{MenuOption{Key: "1", Label: c.T("merchant.buy_potions", nil)}, c.goTo(ViewBuyPotions)},
			{MenuOption{Key: "2", Label: c.T("merchant.sell_items", nil)}, c.goTo(ViewSell)},
			{MenuOption{Key: "3", Label: c.T("merchant.equipment", nil)}, c.goTo(ViewEquipment)},
			{MenuOption{Key: "4", Label: c.T("merchant.stock", nil)}, c.goTo(ViewStock)},
			{MenuOption{Key: "5", Label: c.T("merchant.character", nil)}, c.goTo(ViewCharacter)},
			{MenuOption{Key: "0", Label: c.T("merchant.next_fight", nil)}, c.command(application.NextFight())},
			{MenuOption{Key: "q", Label: c.T("battle.save_quit", nil)}, c.saveAndQuit},
		}
	case ViewBuyPotions:
		return append(c.potionShop(), c.back(ViewMerchant))
	case ViewSell:
		return append(c.sellMenu(), c.back(ViewMerchant))
	case ViewEquipment:
		return append(c.equipmentMenu(), c.back(ViewMerchant))
	case ViewStock:
		return append(c.stockMenu(), c.back(ViewMerchant))
	case ViewCharacter:
		return []menuEntry{c.back(ViewMerchant)}
	case ViewBattle:
		return []menuEntry{
			{MenuOption{Key: "1", Label: c.T("battle.attack", nil)}, c.command(application.Attack())},
			{MenuOption{Key: "2", Label: c.T("battle.spells", nil)}, c.goTo(ViewSpells)},
			{MenuOption{Key: "3", Label: c.T("battle.potions", nil)}, c.goTo(ViewPotions)},
			{MenuOption{Key: "4", Label: c.T("battle.defend", nil)}, c.command(application.Defend())},
			{MenuOption{Key: "q", Label: c.T("battle.save_quit", nil)}, c.saveAndQuit},
		}
	case ViewSpells:
		return append(c.spellMenu(), c.back(ViewBattle))
	case ViewPotions:
		return append(c.battlePotions(), c.back(ViewBattle))
	case ViewGameOver:
		return []menuEntry{
			{MenuOption{Key: "1", Label: c.T("gameover.new_run", nil)}, c.newRun},
			{MenuOption{Key: "2", Label: c.T("gameover.title_screen", nil)}, c.goTo(ViewTitle)},
		}
	case ViewHallOfFame, ViewBestiary, ViewAchievements:
		return []menuEntry{c.back(ViewTitle)}
	default:
		return nil
	}
}

func (c *Controller) back(target View) menuEntry {
	return menuEntry{MenuOption{Key: "0", Label: c.T("menu.back", nil)}, c.goTo(target)}
}

func (c *Controller) goTo(target View) func() {
	return func() {
		c.View = target
		c.Page = 0
	}
}

func (c *Controller) titleMenu() []menuEntry {
	entries := []menuEntry{}

	if save, err := c.Services.Repositories.Saves.Load(); err == nil && save != nil {
		entries = append(entries, menuEntry{MenuOption{Key: "1", Label: c.T("menu.continue", nil)}, c.continueRun})
	}

	return append(entries,
		menuEntry{MenuOption{Key: "2", Label: c.T("menu.new_run", nil)}, c.newRun},
		menuEntry{MenuOption{Key: "3", Label: c.T("menu.hall_of_fame", nil)}, c.goTo(ViewHallOfFame)},
		menuEntry{MenuOption{Key: "4", Label: c.T("menu.bestiary", nil)}, c.goTo(ViewBestiary)},
		menuEntry{MenuOption{Key: "5", Label: c.T("menu.achievements", nil)}, c.goTo(ViewAchievements)},
		menuEntry{MenuOption{Key: "6", Label: c.T("menu.language", nil)}, c.openLanguage},
		menuEntry{MenuOption{Key: "0", Label: c.T("menu.quit", nil)}, func() { c.ExitRequested = true }},
	)
}

func (c *Controller) itemLabel(template string, item domain.ItemInstance, params map[string]any) string {
	definition := c.Services.Data.Item(item.ItemID)
	values := map[string]any{
		"name": definition.Name, "rarity": c.T("rarity."+item.Rarity, nil), "slot": c.T("slot."+string(definition.Slot), nil),
	}

	maps.Copy(values, params)

	return c.T(template, values)
}

func (c *Controller) potionShop() []menuEntry {
	state := c.Session.State()
	entries := []menuEntry{}

	for index, potionID := range application.AvailablePotions(state, c.Services.Data) {
		potion := c.Services.Data.Potion(potionID)
		label := c.T("merchant.potion_option", map[string]any{"name": potion.Name, "price": potion.Price, "count": state.Player.PotionCount(potionID)})
		entries = append(entries, menuEntry{MenuOption{Key: ListKey(index), Label: label}, func() { c.askQuantity(potionID) }})
	}

	return entries
}

func (c *Controller) sellMenu() []menuEntry {
	entries := []menuEntry{}

	for index, item := range c.Session.State().Player.Bag {
		label := c.itemLabel("merchant.sell_option", item, map[string]any{"gold": domain.ItemValue(item, c.Services.Data)})
		entries = append(entries, menuEntry{MenuOption{Key: ListKey(index), Label: label, Color: item.Rarity}, c.command(application.SellItem(item.UID))})
	}

	return entries
}

func (c *Controller) equipmentMenu() []menuEntry {
	player := c.Session.State().Player
	vocation := c.Services.Data.Vocation(player.VocationID)
	entries := []menuEntry{}

	add := func(label, color string, command application.Command) {
		entries = append(entries, menuEntry{MenuOption{Key: ListKey(len(entries)), Label: label, Color: color}, c.command(command)})
	}

	for _, item := range player.Bag {
		if application.CanUse(c.Services.Data.Item(item.ItemID), vocation) {
			add(c.itemLabel("merchant.equip_option", item, nil), item.Rarity, application.Equip(item.UID))
		}
	}

	for _, slot := range domain.Slots {
		if item, ok := player.Equipment[slot]; ok {
			add(c.itemLabel("merchant.unequip_option", item, nil), item.Rarity, application.Unequip(slot))
		}
	}

	return entries
}

func (c *Controller) stockMenu() []menuEntry {
	entries := []menuEntry{}

	for index, item := range c.Session.State().MerchantStock {
		label := c.itemLabel("merchant.stock_option", item, map[string]any{"gold": application.StockPrice(item, c.Services.Data)})
		entries = append(entries, menuEntry{MenuOption{Key: ListKey(index), Label: label, Color: item.Rarity}, c.command(application.BuyStockItem(index))})
	}

	return entries
}

func (c *Controller) spellMenu() []menuEntry {
	data := c.Services.Data
	player := c.Session.State().Player
	entries := []menuEntry{}

	for index, spellID := range data.Vocation(player.VocationID).Spells {
		spell := data.Spell(spellID)
		uses := player.SpellUses[spellID]
		level := domain.SpellLevelForUses(uses, data.Balance.SpellLevels)
		label := c.T("battle.spell_option", map[string]any{
			"name": spell.Name, "words": spell.Words, "mana": domain.Pct(spell.Mana, level.ManaPct), "level": level.Level, "uses": uses,
		})

		color := string(spell.Element)
		if spell.Kind == "heal" {
			color = "heal"
		}

		entries = append(entries, menuEntry{MenuOption{Key: ListKey(index), Label: label, Color: color}, c.command(application.Cast(spellID))})
	}

	return entries
}

func (c *Controller) battlePotions() []menuEntry {
	player := c.Session.State().Player
	entries := []menuEntry{}

	for _, potion := range c.Services.Data.Potions {
		if player.PotionCount(potion.ID) <= 0 {
			continue
		}

		label := c.T("battle.potion_option", map[string]any{"name": potion.Name, "count": player.PotionCount(potion.ID)})
		entries = append(entries, menuEntry{MenuOption{Key: ListKey(len(entries)), Label: label}, c.command(application.UsePotion(potion.ID))})
	}

	return entries
}

// ── actions ───────────────────────────────────────────────────────────────

func (c *Controller) chooseLanguage(locale string) {
	if err := c.setLocale(locale); err != nil {
		c.Err = err

		return
	}

	if err := c.Services.Settings.Save(infrastructure.Settings{Locale: locale}); err != nil {
		c.Err = err
	}

	c.View = c.languageReturn
}

func (c *Controller) openLanguage() {
	c.languageReturn = ViewTitle
	c.View = ViewLanguage
}

func (c *Controller) newRun() {
	c.Session = nil
	c.View = ViewDifficulty
}

func (c *Controller) chooseDifficulty(difficultyID string) {
	c.difficulty = difficultyID
	c.InputBuffer = ""
	c.View = ViewName
}

func (c *Controller) sessionContext() application.SessionContext {
	return application.SessionContext{Repositories: c.Services.Repositories, Clock: c.Services.Clock, GameVersion: c.Services.Version}
}

func (c *Controller) chooseVocation(vocationID string) {
	seed := c.seedSource()
	if c.seed != nil {
		seed = *c.seed
	}

	config := application.RunConfig{Name: c.name, VocationID: vocationID, DifficultyID: c.difficulty}

	session, events, err := application.StartSession(c.Services.Data, config, seed, c.sessionContext())
	if err != nil {
		c.Err = err

		return
	}

	c.Session = session
	c.Log = []string{}
	c.record(application.StepResult{Events: events})
	c.View = ViewMerchant
}

func (c *Controller) continueRun() {
	session, err := application.ResumeSession(c.Services.Data, c.sessionContext())
	if err != nil || session == nil {
		c.Err = err

		return
	}

	c.Session = session
	c.Log = []string{}
	c.pushLog(c.T("menu.welcome_back", map[string]any{"name": session.State().Player.Name, "round": session.State().Round}))
	c.View = ViewMerchant
}

func (c *Controller) askQuantity(potionID string) {
	c.potionID = potionID
	c.InputBuffer = ""
	c.View = ViewQuantity
}

func (c *Controller) command(command application.Command) func() {
	return func() { c.step(command) }
}

func (c *Controller) saveAndQuit() {
	if c.Session != nil {
		if err := c.Session.SaveAndQuit(); err != nil {
			c.Err = err
		}
	}

	c.Session = nil
	c.View = ViewTitle
}

func (c *Controller) step(command application.Command) {
	result, err := c.Session.Step(command)
	if err != nil {
		c.Err = err

		return
	}

	c.record(result)

	switch phase := c.Session.State().Phase; {
	case phase == domain.PhaseBattle:
		c.View = ViewBattle
	case phase == domain.PhaseGameOver:
		c.View = ViewGameOver
	case isBattleView(c.View):
		c.View = ViewMerchant
	}
}

func (c *Controller) pushLog(line string) {
	c.Log = append(c.Log, line)
	if len(c.Log) > maxLogLines {
		c.Log = c.Log[len(c.Log)-maxLogLines:]
	}
}

func (c *Controller) record(result application.StepResult) {
	state := c.Session.State()
	cues := []string{}

	for _, evt := range result.Events {
		text := c.formatter.Format(evt, state)
		if evt.Type() == "error" {
			c.Message = text

			continue
		}

		c.pushLog(text)

		switch {
		case (evt.Type() == "player_attacked" || evt.Type() == "spell_cast") && evt.Int("damage") != 0:
			cues = append(cues, "hurt")
		case evt.Type() == "monster_attacked":
			cues = append(cues, "attack")
		}
	}

	for _, achievement := range result.Achievements {
		name := c.T("achievement."+achievement.ID+".name", nil)
		c.pushLog(c.T("achievement.unlocked", map[string]any{"name": name}))
	}

	c.AnimationCues = cues
}
