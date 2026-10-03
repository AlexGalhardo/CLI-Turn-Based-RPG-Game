package application_test

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"testing"
	"time"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

type fakeClock struct {
	current time.Time
}

func (c *fakeClock) Now() time.Time {
	c.current = c.current.Add(10 * time.Second)

	return c.current
}

func sessionContext(dir string, clock application.Clock) application.SessionContext {
	return application.SessionContext{
		Repositories: application.Repositories{
			Saves:   infrastructure.NewFileSaveRepository(dir),
			History: infrastructure.NewFileHistoryRepository(dir),
			Profile: infrastructure.NewFileProfileRepository(dir),
		},
		Clock:       clock,
		GameVersion: "9.9.9",
	}
}

func newClock() *fakeClock {
	return &fakeClock{current: time.Date(2026, 9, 27, 12, 0, 0, 0, time.UTC)}
}

func TestStartSession(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()

	session, events, err := application.StartSession(testData(t), application.RunConfig{Name: "Alex", VocationID: "archer", DifficultyID: "normal"}, 5, sessionContext(dir, newClock()))
	if err != nil || events[0].Type() != "run_started" {
		t.Fatalf("start: %v", err)
	}

	var save map[string]any

	content, _ := os.ReadFile(filepath.Join(dir, "save.json"))
	if err := json.Unmarshal(content, &save); err != nil || save["implementation"] != "golang" || save["gameVersion"] != "9.9.9" {
		t.Fatalf("save.json = %v (%v)", save, err)
	}

	if _, err := session.Step(application.BuyPotion("health_potion", 1)); err != nil {
		t.Fatal(err)
	}

	reloaded, err := infrastructure.NewFileSaveRepository(dir).Load()
	if err != nil || reloaded.Run.Player.PotionCount("health_potion") != 6 {
		t.Fatalf("autosave after merchant actions: %v", err)
	}

	if _, _, err := application.StartSession(testData(t), application.RunConfig{Name: "X", VocationID: "knight", DifficultyID: "normal"}, 1, sessionContext(dir, newClock())); !errors.Is(err, application.ErrInvalidRunConfig) {
		t.Fatal("invalid config must fail")
	}
}

func TestResumeSession(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	clock := newClock()
	data := testData(t)

	session, _, err := application.StartSession(data, application.RunConfig{Name: "Alex", VocationID: "warrior", DifficultyID: "normal"}, 5, sessionContext(dir, clock))
	if err != nil {
		t.Fatal(err)
	}

	_, _ = session.Step(application.NextFight())
	session.State().Monster.HP, session.State().Monster.MaxHP = 1_000_000, 1_000_000
	_, _ = session.Step(application.Attack())

	if session.State().Phase != domain.PhaseBattle {
		t.Fatal("the fight must still be going on")
	}

	if err := session.SaveAndQuit(); err != nil {
		t.Fatal(err)
	}

	resumed, err := application.ResumeSession(data, sessionContext(dir, clock))
	if err != nil || resumed.State().Phase != domain.PhaseMerchant || resumed.State().Round != 0 || resumed.Info.Sessions != 2 || resumed.Info.PlayTimeSeconds <= 0 {
		t.Fatalf("resume from the last merchant snapshot: %+v %v", resumed, err)
	}

	if none, err := application.ResumeSession(data, sessionContext(t.TempDir(), clock)); none != nil || err != nil {
		t.Fatal("no save means no session")
	}
}

func TestGameSession_Death(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	data := testData(t)
	context := sessionContext(dir, newClock())

	session, _, err := application.StartSession(data, application.RunConfig{Name: "Bot", VocationID: "mage", DifficultyID: "hard"}, 3, context)
	if err != nil {
		t.Fatal(err)
	}

	bot := application.NewGreedyBot(data)
	unlocked := []string{}

	for session.State().Phase != domain.PhaseGameOver {
		result, err := session.Step(bot.Choose(session.State()))
		if err != nil {
			t.Fatal(err)
		}

		for _, achievement := range result.Achievements {
			unlocked = append(unlocked, achievement.ID)
		}
	}

	if _, err := os.Stat(filepath.Join(dir, "save.json")); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("death deletes the save")
	}

	records, err := context.Repositories.History.List()
	if err != nil || len(records) != 1 {
		t.Fatalf("history: %v %v", records, err)
	}

	started, _ := application.ParseTimestamp(records[0].StartedAt)
	ended, _ := application.ParseTimestamp(records[0].EndedAt)

	if !ended.After(started) || !slices.Contains(unlocked, "first_blood") {
		t.Fatal("timestamps and achievements must be recorded")
	}

	profile, err := context.Repositories.Profile.Load()
	if err != nil || profile.HallOfFame[0].RunID != records[0].RunID {
		t.Fatalf("hall of fame: %v", err)
	}

	if err := session.SaveAndQuit(); err != nil {
		t.Fatal("quitting after death is a no-op")
	}
}

func TestProfileService_RecordFinishedRun(t *testing.T) {
	t.Parallel()

	service := application.NewProfileService(testData(t), application.NewProfile())
	for i := range 12 {
		service.RecordFinishedRun(application.HallOfFameEntry{
			RunID: "run", Round: i % 5, Level: i, EndedAt: time.Date(2026, 1, i+1, 0, 0, 0, 0, time.UTC).Format(time.RFC3339),
		})
	}

	hall := service.Profile.HallOfFame
	if len(hall) != application.HallOfFameSize || !reflect.DeepEqual([]int{hall[0].Round, hall[1].Round, hall[2].Round}, []int{4, 4, 3}) || hall[0].Level <= hall[1].Level {
		t.Fatalf("hall of fame order: %+v", hall)
	}

	if service.Revealed("rat") {
		t.Fatal("unknown creatures are not revealed")
	}
}

func TestSaveGameFormat(t *testing.T) {
	t.Parallel()

	if application.MakeRunID(time.Date(2026, 1, 2, 3, 4, 5, 0, time.UTC), 42) != "20260102T030405Z-42" {
		t.Fatal("run id format")
	}

	if _, err := application.ParseTimestamp("yesterday"); err == nil {
		t.Fatal("bad timestamps fail")
	}

	if err := application.CheckSchema([]byte(`{"schemaVersion":99}`), "x"); !errors.Is(err, application.ErrNewerSchema) {
		t.Fatal("newer schemas are refused")
	}

	for _, raw := range []string{`{`, `{"schemaVersion":1,"run":{}}`, `{"schemaVersion":1}`} {
		if _, err := application.SaveGameFromJSON([]byte(raw)); err == nil {
			t.Fatalf("%s must fail", raw)
		}
	}
}

func TestSimulate(t *testing.T) {
	t.Parallel()

	data := testData(t)

	summary, err := application.Simulate(data, "warrior", "normal", 3, 10)
	if err != nil || summary.Runs != 3 || summary.MinRound > summary.MedianRound || len(summary.TopKillers) == 0 {
		t.Fatalf("summary: %+v %v", summary, err)
	}

	if _, err := application.Simulate(data, "warrior", "normal", 0, 1); !errors.Is(err, application.ErrInvalidSimulation) {
		t.Fatal("zero runs must fail")
	}

	if _, err := application.Simulate(data, "knight", "normal", 1, 1); err == nil {
		t.Fatal("unknown vocation must fail")
	}
}

func TestRestoreMidRun(t *testing.T) {
	t.Parallel()

	data := testData(t)
	bot := application.NewGreedyBot(data)
	config := application.RunConfig{Name: "Bot", VocationID: "mage", DifficultyID: "hard"}

	reference, _, _ := application.NewRun(data, config, 99)
	referenceLog := [][]application.Event{}

	for reference.State.Phase != domain.PhaseGameOver {
		referenceLog = append(referenceLog, reference.Step(bot.Choose(reference.State)))
	}

	engine, _, _ := application.NewRun(data, config, 99)
	log := [][]application.Event{}

	for engine.State.Phase != domain.PhaseGameOver {
		if engine.State.Phase == domain.PhaseMerchant && engine.State.Round%3 == 0 {
			engine = application.Restore(data, engine.State.Clone(), engine.RngState())
		}

		log = append(log, engine.Step(bot.Choose(engine.State)))
	}

	if !reflect.DeepEqual(log, referenceLog) {
		t.Fatal("restoring at the merchant must not change the run")
	}
}
