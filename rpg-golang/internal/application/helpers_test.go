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
