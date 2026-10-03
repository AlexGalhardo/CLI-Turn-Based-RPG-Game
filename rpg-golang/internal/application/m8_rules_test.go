package application_test

// M8 additions to the merchant/loot, profile, simulator, full-run and serialization tests of the reference.

import (
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

func TestRollRaritySkipsZeroWeightsAndSingleOptions(t *testing.T) {
	t.Parallel()

	data := testData(t)
	rng := domain.NewRng(1)

	if application.RollRarity(data, rng, map[string]int{"rare": 5}).ID != "rare" ||
		application.RollRarity(data, rng, map[string]int{"common": 0, "mythic": 3}).ID != "mythic" || rng.State() != 1 {
		t.Fatal("a single positive weight is not rolled")
	}

	rolled := map[string]bool{}
	for range 40 {
		rolled[application.RollRarity(data, rng, map[string]int{"common": 1, "legendary": 1}).ID] = true
	}

	if !reflect.DeepEqual(rolled, map[string]bool{"common": true, "legendary": true}) || rng.State() == 1 {
		t.Fatalf("rolled = %v", rolled)
	}

	defer func() {
		if recover() == nil {
			t.Fatal("a table without a positive weight is inconsistent data")
		}
	}()

	application.RollRarity(data, rng, map[string]int{"common": 0})
}

func TestRaritiesScaleBaseStatsAndAffixCounts(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)

	for _, tt := range []struct {
		rarity          string
		attack, affixes int
	}{{"common", 20, 0}, {"rare", 30, 1}, {"legendary", 40, 2}, {"mythic", 60, 2}} {
		stats := domain.ItemStats(domain.ItemInstance{UID: 1, ItemID: "test_axe", Rarity: tt.rarity}, data)
		definition := data.Balance.MustRarity(tt.rarity)

		if !reflect.DeepEqual(stats, map[domain.Stat]int{domain.StatAttack: tt.attack}) || definition.AffixMin != tt.affixes || definition.AffixMax != tt.affixes {
			t.Fatalf("%s: %v", tt.rarity, stats)
		}
	}
}

func TestItemScoreWeightsFinalStats(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	weights := data.Balance.ItemScoreWeights
	common := domain.ItemInstance{UID: 1, ItemID: "test_helmet", Rarity: "common"}

	if domain.ItemScore(common, data) != 10*weights[domain.StatArmor]+50*weights[domain.StatMaxHp] {
		t.Fatal("score = Σ stat × weight")
	}

	scores := []int{}
	for _, rarity := range []string{"common", "rare", "legendary"} {
		scores = append(scores, domain.ItemScore(domain.ItemInstance{UID: 1, ItemID: "test_helmet", Rarity: rarity}, data))
	}

	if !slices.IsSorted(scores) || scores[0] == scores[1] || scores[1] == scores[2] {
		t.Fatalf("scores = %v", scores)
	}

	withAffix := common
	withAffix.Affixes = []domain.AffixRoll{{Stat: domain.StatDodge, Value: 2}}

	if domain.ItemScore(withAffix, data) != domain.ItemScore(common, data)+2*weights[domain.StatDodge] {
		t.Fatal("affixes count in the score")
	}
}

func TestRequiredLevelGrowsWithTheItemTier(t *testing.T) {
	t.Parallel()

	data := testData(t)
	perTier := data.Balance.ItemLevelPerTier

	if domain.RequiredLevel(domain.ItemInstance{ItemID: "sword", Tier: 0}, data) != 1 ||
		domain.RequiredLevel(domain.ItemInstance{ItemID: "sword", Tier: 3}, data) != 1+3*perTier {
		t.Fatal("requiredLevel = 1 + tier × itemLevelPerTier")
	}
}

func TestEquipRejectsItemsAboveThePlayerLevel(t *testing.T) {
	t.Parallel()

	data := withTestItems(t)
	engine := newEngine(t, data, "warrior", "normal", 42)
	player := engine.State.Player
	axe := domain.ItemInstance{UID: 60, ItemID: "test_axe", Rarity: "common", Tier: 5, Affixes: []domain.AffixRoll{}}
	player.Bag = append(player.Bag, axe)
	rngState := engine.RngState()

	if events := engine.Step(application.Equip(60)); !reflect.DeepEqual(events, []application.Event{application.ErrorEvent(application.ErrLevelTooLow)}) ||
		!slices.ContainsFunc(player.Bag, func(item domain.ItemInstance) bool { return item.UID == 60 }) || engine.RngState() != rngState {
		t.Fatalf("level too low: %v", events)
	}

	player.Level = domain.RequiredLevel(axe, data)
	events := engine.Step(application.Equip(60))

	if !reflect.DeepEqual(events[len(events)-1], application.Event{"type": "item_equipped", "uid": 60, "itemId": "test_axe", "slot": "weapon"}) {
		t.Fatalf("equip = %v", events)
	}
}

func TestSellingAnEquippedUIDIsRejected(t *testing.T) {
	t.Parallel()

	engine := newEngine(t, testData(t), "warrior", "normal", 42)
	player := engine.State.Player
	weapon := player.Equipment[domain.SlotWeapon]
	gold := player.Gold

	if events := engine.Step(application.SellItem(weapon.UID)); !reflect.DeepEqual(events, []application.Event{application.ErrorEvent(application.ErrInvalidItem)}) ||
		!reflect.DeepEqual(player.Equipment[domain.SlotWeapon], weapon) || player.Gold != gold {
		t.Fatalf("sell equipped: %v", events)
	}
}

func TestHallOfFamePutsWonRunsFirst(t *testing.T) {
	t.Parallel()

	service := application.NewProfileService(testData(t), application.NewProfile())
	service.RecordFinishedRun(application.HallOfFameEntry{RunID: "deep", Round: 90, Level: 60, EndedAt: "2026-01-01T00:00:00Z"})
	service.RecordFinishedRun(application.HallOfFameEntry{RunID: "winner", Round: 1, Level: 1, EndedAt: "2026-02-01T00:00:00Z", Won: true})

	if service.Profile.HallOfFame[0].RunID != "winner" {
		t.Fatal("won runs rank first")
	}
}

func TestNewSessionSaveCarriesTheM8Fields(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	if _, _, err := application.StartSession(testData(t), application.RunConfig{Name: "Alex", VocationID: "archer", DifficultyID: "normal"}, 5, sessionContext(dir, newClock())); err != nil {
		t.Fatal(err)
	}

	content, err := os.ReadFile(filepath.Join(dir, "save.json"))
	if err != nil {
		t.Fatal(err)
	}

	var save struct {
		SchemaVersion int `json:"schemaVersion"`
		Run           struct {
			Config map[string]any `json:"config"`
			Won    *bool          `json:"won"`
		} `json:"run"`
	}
	if err := json.Unmarshal(content, &save); err != nil {
		t.Fatal(err)
	}

	if save.SchemaVersion != 2 || save.Run.Config["autoEquip"] != false || save.Run.Won == nil || *save.Run.Won {
		t.Fatalf("save = %s", content)
	}
}

func TestSimulatorCountsWonRuns(t *testing.T) {
	t.Parallel()

	data := testData(t)

	summary, err := application.Simulate(data, "archer", "easy", 2, 2002)
	if err != nil || summary.Wins < 1 || summary.MaxRound != data.Balance.FinalRound || summary.WinRatePct() != summary.Wins*100/2 {
		t.Fatalf("summary = %+v %v", summary, err)
	}
}

func playToEnd(t *testing.T, engine *application.GameEngine) [][]application.Event {
	t.Helper()

	bot := application.NewGreedyBot(engine.Data)
	log := [][]application.Event{}

	for range 200_000 {
		if engine.State.Phase == domain.PhaseGameOver {
			return log
		}

		log = append(log, engine.Step(bot.Choose(engine.State)))
	}

	t.Fatal("the run did not end")

	return nil
}

func TestBotPlaysUntilTheRunEnds(t *testing.T) {
	t.Parallel()

	data := testData(t)

	for _, vocation := range []string{"warrior", "archer", "mage"} {
		for _, difficulty := range []string{"easy", "normal", "hard"} {
			engine := newEngine(t, data, vocation, difficulty, 1234)
			log := playToEnd(t, engine)
			state := engine.State

			if state.Round < 1 || state.Stats.DamageDealt <= 0 {
				t.Fatalf("%s/%s: nothing happened", vocation, difficulty)
			}

			last := log[len(log)-1]

			if state.Won {
				beforeLast := log[len(log)-2]
				if state.Round != data.Balance.FinalRound || state.DeathCause != nil || state.Stats.TotalKills() != state.Round ||
					!reflect.DeepEqual(beforeLast[len(beforeLast)-1], application.Event{"type": "run_won", "round": state.Round}) ||
					!reflect.DeepEqual(last, []application.Event{{"type": "run_ended", "won": true}}) {
					t.Fatalf("%s/%s: won run", vocation, difficulty)
				}

				continue
			}

			if state.DeathCause == nil || last[len(last)-1].Type() != "player_died" || state.Stats.TotalKills() != state.Round-1 {
				t.Fatalf("%s/%s: lost run", vocation, difficulty)
			}
		}
	}
}

func TestSomeBotRunsAreWon(t *testing.T) {
	t.Parallel()

	data := testData(t)
	won := 0

	for seed := uint64(2002); seed < 2006; seed++ {
		engine := newEngine(t, data, "archer", "easy", seed)
		playToEnd(t, engine)

		if engine.State.Won {
			won++
		}
	}

	if won == 0 {
		t.Fatal("the victory must stay reachable")
	}
}

func TestVictoryCommandsRoundTrip(t *testing.T) {
	t.Parallel()

	for _, command := range []application.Command{application.EndRun(), application.ContinueRun()} {
		raw, err := json.Marshal(command.ToMap())
		if err != nil {
			t.Fatal(err)
		}

		parsed, err := application.CommandFromJSON(raw)
		if err != nil || !reflect.DeepEqual(parsed, command) || command.IsBattle() {
			t.Fatalf("%s round trip: %+v %v", command.Type, parsed, err)
		}
	}
}
