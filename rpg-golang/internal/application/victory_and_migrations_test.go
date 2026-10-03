package application_test

// Victory phase through the session (save, resume, history, Hall of Fame) and schema 1 → 2 migrations.

import (
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

const maxSwings = 200

type jsonObject = map[string]any

func winFinalFight(t *testing.T, dir string) (*application.GameSession, *fakeClock) {
	t.Helper()

	data := testData(t)
	clock := newClock()

	session, _, err := application.StartSession(data, application.RunConfig{Name: "Vic", VocationID: "warrior", DifficultyID: "easy", AutoEquip: true}, 8, sessionContext(dir, clock))
	if err != nil {
		t.Fatal(err)
	}

	session.State().Round = data.Balance.FinalRound - 1
	mustStep(t, session, application.NextFight())

	monster := session.State().Monster
	if monster.CreatureID != "ferumbras" || monster.EnemyClass != "boss" {
		t.Fatalf("final boss = %s (%s)", monster.CreatureID, monster.EnemyClass)
	}

	for range maxSwings {
		if session.State().Phase != domain.PhaseBattle {
			break
		}

		session.State().Player.HP = 1_000_000
		monster.HP = 1

		mustStep(t, session, application.Attack())
	}

	if session.State().Phase != domain.PhaseVictory {
		t.Fatal("victory phase expected")
	}

	return session, clock
}

func mustStep(t *testing.T, session *application.GameSession, command application.Command) []application.Event {
	t.Helper()

	result, err := session.Step(command)
	if err != nil {
		t.Fatal(err)
	}

	return result.Events
}

func readJSON(t *testing.T, path string) jsonObject {
	t.Helper()

	content, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}

	var document jsonObject
	if err := json.Unmarshal(content, &document); err != nil {
		t.Fatal(err)
	}

	return document
}

func writeJSON(t *testing.T, path string, document any) {
	t.Helper()

	content, err := json.Marshal(document)
	if err != nil {
		t.Fatal(err)
	}

	if err := os.MkdirAll(filepath.Dir(path), 0o750); err != nil {
		t.Fatal(err)
	}

	if err := os.WriteFile(path, content, 0o600); err != nil {
		t.Fatal(err)
	}
}

func object(value any) jsonObject {
	result, _ := value.(jsonObject)

	return result
}

func TestVictoryIsSavedResumedAndEndedAsWon(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	_, clock := winFinalFight(t, dir)

	save := readJSON(t, filepath.Join(dir, "save.json"))
	if run := object(save["run"]); run["phase"] != "victory" || run["won"] != true {
		t.Fatalf("save = %v", run["phase"])
	}

	resumed, err := application.ResumeSession(testData(t), sessionContext(dir, clock))
	if err != nil || resumed == nil || resumed.State().Phase != domain.PhaseVictory {
		t.Fatal("the victory phase is resumed")
	}

	if events := mustStep(t, resumed, application.EndRun()); !reflect.DeepEqual(events, []application.Event{{"type": "run_ended", "won": true}}) {
		t.Fatalf("end_run = %v", events)
	}

	profile, err := infrastructure.NewFileProfileRepository(dir).Load()
	if _, unlocked := profile.Achievements["conqueror"]; err != nil || !unlocked || !profile.HallOfFame[0].Won {
		t.Fatal("conqueror unlocked and a won Hall of Fame entry")
	}

	if _, err := os.Stat(filepath.Join(dir, "save.json")); !os.IsNotExist(err) {
		t.Fatal("the save is deleted")
	}

	records, err := infrastructure.NewFileHistoryRepository(dir).List()
	if err != nil || !records[0].Won || records[0].DeathCause != "" {
		t.Fatal("the history record is won without a cause of death")
	}
}

func TestContinueAfterVictoryKeepsTheRunWon(t *testing.T) {
	t.Parallel()

	data := testData(t)
	session, _ := winFinalFight(t, t.TempDir())

	events := mustStep(t, session, application.ContinueRun())
	if !reflect.DeepEqual(events, []application.Event{{"type": "merchant_entered", "round": data.Balance.FinalRound}}) {
		t.Fatalf("continue_run = %v", events)
	}

	if session.State().Phase != domain.PhaseMerchant || !session.State().Won {
		t.Fatal("merchant phase, still won")
	}

	mustStep(t, session, application.NextFight())

	if session.State().Round != data.Balance.FinalRound+1 {
		t.Fatal("the run goes on")
	}
}

// v1 turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity.
func v1(document jsonObject) jsonObject {
	document["schemaVersion"] = 1
	run := object(document["run"])
	delete(object(run["config"]), "autoEquip")
	delete(run, "won")
	object(object(object(run["player"])["equipment"])["weapon"])["rarity"] = "epic"

	stats := object(run["stats"])
	for _, key := range []string{"itemsAutoEquipped", "elitesKilled", "potionsDropped"} {
		delete(stats, key)
	}

	stats["itemsDropped"] = jsonObject{"epic": 2, "legendary": 1}
	stats["droppedItems"] = []any{jsonObject{"itemId": "sword", "rarity": "epic", "round": 3}}

	return document
}

func TestV1SaveIsMigrated(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	if _, _, err := application.StartSession(testData(t), application.RunConfig{Name: "Old", VocationID: "warrior", DifficultyID: "normal"}, 4, sessionContext(dir, newClock())); err != nil {
		t.Fatal(err)
	}

	path := filepath.Join(dir, "save.json")
	writeJSON(t, path, v1(readJSON(t, path)))

	loaded, err := infrastructure.NewFileSaveRepository(dir).Load()
	if err != nil {
		t.Fatal(err)
	}

	run := loaded.Run
	if run.Config.AutoEquip || run.Won || run.Player.Equipment[domain.SlotWeapon].Rarity != "legendary" {
		t.Fatal("v1 defaults and the epic rarity become legendary")
	}

	if !reflect.DeepEqual(run.Stats.ItemsDropped, map[string]int{"legendary": 3}) || run.Stats.DroppedItems[0].Rarity != "legendary" ||
		run.Stats.ElitesKilled != 0 || run.Stats.PotionsDropped == nil || loaded.SchemaVersion != application.SchemaVersion {
		t.Fatalf("stats = %+v", run.Stats)
	}
}

func TestV1MonsterGetsItsClassFromIsBoss(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()

	session, _, err := application.StartSession(testData(t), application.RunConfig{Name: "Old", VocationID: "mage", DifficultyID: "normal"}, 4, sessionContext(dir, newClock()))
	if err != nil {
		t.Fatal(err)
	}

	session.State().Round = 9
	mustStep(t, session, application.NextFight())

	path := filepath.Join(dir, "save.json")
	document := readJSON(t, path)

	var monster jsonObject

	raw, _ := json.Marshal(session.State().Monster)
	if err := json.Unmarshal(raw, &monster); err != nil {
		t.Fatal(err)
	}

	object(document["run"])["monster"] = monster
	old := v1(document)
	delete(object(object(old["run"])["monster"]), "enemyClass")
	writeJSON(t, path, old)

	loaded, err := infrastructure.NewFileSaveRepository(dir).Load()
	if err != nil || loaded.Run.Monster == nil || loaded.Run.Monster.EnemyClass != "boss" {
		t.Fatalf("migrated monster: %v", err)
	}
}

func TestV1HistoryAndProfileAreMigrated(t *testing.T) {
	t.Parallel()

	root := t.TempDir()
	won := filepath.Join(root, "won")
	old := filepath.Join(root, "old")
	session, _ := winFinalFight(t, won)
	mustStep(t, session, application.EndRun())

	entries, err := os.ReadDir(filepath.Join(won, "history"))
	if err != nil || len(entries) != 1 {
		t.Fatal("one history record")
	}

	record := readJSON(t, filepath.Join(won, "history", entries[0].Name()))
	record["schemaVersion"] = 1
	delete(record, "won")

	for _, key := range []string{"itemsAutoEquipped", "elitesKilled", "potionsDropped"} {
		delete(object(record["stats"]), key)
	}

	writeJSON(t, filepath.Join(old, "history", entries[0].Name()), record)

	records, err := infrastructure.NewFileHistoryRepository(old).List()
	if err != nil || records[0].Won || records[0].Stats.PotionsDropped == nil {
		t.Fatalf("migrated history: %v", err)
	}

	profile := readJSON(t, filepath.Join(won, "profile.json"))
	profile["schemaVersion"] = 1

	hall, _ := profile["hallOfFame"].([]any)
	for _, entry := range hall {
		delete(object(entry), "won")
	}

	writeJSON(t, filepath.Join(old, "profile.json"), profile)

	loaded, err := infrastructure.NewFileProfileRepository(old).Load()
	if err != nil || loaded.HallOfFame[0].Won {
		t.Fatalf("migrated profile: %v", err)
	}
}
