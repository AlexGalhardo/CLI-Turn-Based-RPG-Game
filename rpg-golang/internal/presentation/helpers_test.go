package presentation_test

import (
	"sync"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/version"
)

var loadOnce = sync.OnceValues(func() (*domain.GameData, error) {
	return infrastructure.LoadGameData(assets.Shared())
})

func testData(t *testing.T) *domain.GameData {
	t.Helper()

	data, err := loadOnce()
	if err != nil {
		t.Fatalf("load game data: %v", err)
	}

	return data
}

func services(t *testing.T, dir string) presentation.Services {
	t.Helper()

	return presentation.Services{
		Data:     testData(t),
		Shared:   assets.Shared(),
		Settings: infrastructure.NewSettingsRepository(dir),
		Repositories: application.Repositories{
			Saves:   infrastructure.NewFileSaveRepository(dir),
			History: infrastructure.NewFileHistoryRepository(dir),
			Profile: infrastructure.NewFileProfileRepository(dir),
		},
		Clock:   infrastructure.SystemClock{},
		Version: version.Version,
	}
}

func newController(t *testing.T, dir, locale string) *presentation.Controller {
	t.Helper()

	seed := uint64(7)

	controller, err := presentation.NewController(services(t, dir), &seed, locale, nil)
	if err != nil {
		t.Fatal(err)
	}

	return controller
}

func press(controller *presentation.Controller, keys ...string) {
	for _, key := range keys {
		controller.Press(key)
	}
}
