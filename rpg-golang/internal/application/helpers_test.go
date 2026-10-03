package application_test

import (
	"sync"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

var loadOnce = sync.OnceValues(func() (*domain.GameData, error) {
	return infrastructure.LoadGameData(assets.Shared())
})

// testData returns the shared game data. Tests that mutate definitions must use freshData instead.
func testData(t *testing.T) *domain.GameData {
	t.Helper()

	data, err := loadOnce()
	if err != nil {
		t.Fatalf("load game data: %v", err)
	}

	return data
}

// freshData returns an independent copy of the game data that a test may modify.
func freshData(t *testing.T) *domain.GameData {
	t.Helper()

	data, err := infrastructure.LoadGameData(assets.Shared())
	if err != nil {
		t.Fatalf("load game data: %v", err)
	}

	return data
}

func newEngine(t *testing.T, data *domain.GameData, vocation, difficulty string, seed uint64) *application.GameEngine {
	t.Helper()

	engine, _, err := application.NewRun(data, application.RunConfig{Name: "Tester", VocationID: vocation, DifficultyID: difficulty}, seed)
	if err != nil {
		t.Fatalf("new run: %v", err)
	}

	return engine
}

// withEnemyClass changes one balance.enemyClasses row of a fresh data copy (chances of 0/100 make the RNG outcome
// certain).
func withEnemyClass(data *domain.GameData, classID string, change func(row *domain.EnemyClassDef)) *domain.GameData {
	for i := range data.Balance.EnemyClasses {
		if data.Balance.EnemyClasses[i].ID == classID {
			change(&data.Balance.EnemyClasses[i])
		}
	}

	return data
}

// calmData is a fresh data copy with no monster dodge, parry, crit or heal and no elites: the pre-M8 fight, for
// tests of other mechanics.
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

func eventTypes(events []application.Event) []string {
	types := make([]string, len(events))
	for i, evt := range events {
		types[i] = evt.Type()
	}

	return types
}

func lookupEvent(events []application.Event, eventType string) (application.Event, bool) {
	for _, evt := range events {
		if evt.Type() == eventType {
			return evt, true
		}
	}

	return nil, false
}
