package infrastructure_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

func TestBalanceM8Tables(t *testing.T) {
	t.Parallel()

	data, err := infrastructure.LoadGameData(assets.Shared())
	if err != nil {
		t.Fatal(err)
	}

	balance := data.Balance
	rarities, statPcts, effects := []string{}, []int{}, []int{}

	for _, rarity := range balance.Rarities {
		rarities = append(rarities, rarity.ID)
		statPcts = append(statPcts, rarity.StatPct)
	}

	for _, level := range balance.SpellLevels {
		effects = append(effects, level.EffectPct)
	}

	if !reflect.DeepEqual(rarities, []string{"common", "rare", "legendary", "mythic"}) || !reflect.DeepEqual(statPcts, []int{100, 150, 200, 300}) ||
		!reflect.DeepEqual(effects, []int{100, 150, 200}) {
		t.Fatalf("rarities %v %v, spell effects %v", rarities, statPcts, effects)
	}

	if _, ok := balance.RarityWeights["merchant"]; !ok || len(balance.RarityWeights) != 1 {
		t.Fatal("only the merchant table remains in rarityWeights")
	}

	for _, row := range balance.EnemyClasses {
		for rarity := range row.RarityWeights {
			if _, err := balance.Rarity(rarity); err != nil {
				t.Fatalf("%s: unknown rarity %s", row.ID, rarity)
			}
		}
	}

	if balance.MustEnemyClass("elite").StatPct <= balance.MustEnemyClass("normal").StatPct || len(balance.EnemyClasses) != 3 {
		t.Fatal("elites are stronger")
	}

	if len(balance.ItemScoreWeights) != len(domain.Stats) {
		t.Fatal("every stat has a score weight")
	}

	for _, stat := range domain.Stats {
		if _, ok := balance.ItemScoreWeights[stat]; !ok {
			t.Fatalf("missing weight for %s", stat)
		}
	}

	modes := []string{}
	for _, mode := range balance.AutoBattle.Modes {
		modes = append(modes, mode.ID)
	}

	if !reflect.DeepEqual(modes, []string{"melee", "spells", "balanced"}) {
		t.Fatalf("auto-battle modes in file order: %v", modes)
	}

	if data.BossOfTier(data.TierCount()-1).ID != "ferumbras" || balance.FinalRound != balance.RoundsPerTier*data.TierCount() {
		t.Fatal("the final boss is Ferumbras on the last round of the first cycle")
	}
}

func readSettingsFile(t *testing.T, dir string) map[string]any {
	t.Helper()

	content, err := os.ReadFile(filepath.Join(dir, "settings.json"))
	if err != nil {
		t.Fatal(err)
	}

	var document map[string]any
	if err := json.Unmarshal(content, &document); err != nil {
		t.Fatal(err)
	}

	return document
}

func TestSettingsRoundTripAndMigration(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	repository := infrastructure.NewSettingsRepository(dir)
	write := func(raw string) {
		if err := os.WriteFile(filepath.Join(dir, "settings.json"), []byte(raw), 0o600); err != nil {
			t.Fatal(err)
		}
	}

	if got, err := repository.Load(); err != nil || got != infrastructure.DefaultSettings() {
		t.Fatalf("defaults = %+v", got)
	}

	full := infrastructure.Settings{Locale: "pt-BR", AutoEquip: true, BattleSpeed: 2}
	if err := repository.Save(full); err != nil {
		t.Fatal(err)
	}

	if got, _ := repository.Load(); got != full {
		t.Fatalf("round trip = %+v", got)
	}

	if document := readSettingsFile(t, dir); !reflect.DeepEqual(document, map[string]any{"schemaVersion": 2.0, "locale": "pt-BR", "autoEquip": true, "battleSpeed": 2.0}) {
		t.Fatalf("settings.json = %v", document)
	}

	if err := repository.Save(infrastructure.DefaultSettings()); err != nil {
		t.Fatal(err)
	}

	if got, _ := repository.Load(); got != infrastructure.DefaultSettings() {
		t.Fatal("default round trip")
	}

	write(`{"schemaVersion":1,"locale":"en"}`)

	if got, _ := repository.Load(); got != (infrastructure.Settings{Locale: "en", BattleSpeed: 1}) {
		t.Fatalf("v1 settings = %+v", got)
	}

	write(`{"schemaVersion":2,"autoEquip":true,"battleSpeed":7}`)

	if got, _ := repository.Load(); got != (infrastructure.Settings{AutoEquip: true, BattleSpeed: 1}) {
		t.Fatalf("unknown speed = %+v", got)
	}

	write(`{"schemaVersion":1,`)

	if _, err := repository.Load(); err == nil {
		t.Fatal("broken settings fail")
	}
}

func TestMigrationsKeepCurrentDocumentsAndRejectBrokenOnes(t *testing.T) {
	t.Parallel()

	current := []byte(`{"schemaVersion":2,"won":true}`)
	for _, migrate := range []func([]byte) ([]byte, error){
		infrastructure.MigrateSave, infrastructure.MigrateHistory, infrastructure.MigrateProfile, infrastructure.MigrateSettings,
	} {
		if got, err := migrate(current); err != nil || string(got) != string(current) {
			t.Fatal("current documents are untouched")
		}

		if _, err := migrate([]byte(`[`)); err == nil {
			t.Fatal("broken documents fail")
		}
	}

	migrated, err := infrastructure.MigrateHistory([]byte(`{"stats":{"itemsDropped":{"epic":1}}}`))
	if err != nil {
		t.Fatal(err)
	}

	var document map[string]any
	if err := json.Unmarshal(migrated, &document); err != nil {
		t.Fatal(err)
	}

	stats, _ := document["stats"].(map[string]any)
	if document["schemaVersion"] != 2.0 || document["won"] != false || !reflect.DeepEqual(stats["itemsDropped"], map[string]any{"legendary": 1.0}) {
		t.Fatalf("migrated = %s", migrated)
	}
}
