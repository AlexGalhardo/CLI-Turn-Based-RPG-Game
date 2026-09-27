package infrastructure_test

import (
	"errors"
	"os"
	"path/filepath"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

func TestRepositories(t *testing.T) {
	t.Parallel()

	dir := t.TempDir()
	settings := infrastructure.NewSettingsRepository(dir)

	if got, err := settings.Load(); err != nil || got.Locale != "" {
		t.Fatal("no settings yet")
	}

	if err := settings.Save(infrastructure.Settings{Locale: "pt-BR"}); err != nil {
		t.Fatal(err)
	}

	if got, _ := settings.Load(); got.Locale != "pt-BR" {
		t.Fatal("settings round trip")
	}

	_ = os.WriteFile(filepath.Join(dir, "settings.json"), []byte(`{"schemaVersion":1,"locale":"fr"}`), 0o600)

	if got, _ := settings.Load(); got.Locale != "" {
		t.Fatal("unknown locales are ignored")
	}

	_ = os.WriteFile(filepath.Join(dir, "save.json"), []byte(`{"schemaVersion":99}`), 0o600)
	if _, err := infrastructure.NewFileSaveRepository(dir).Load(); !errors.Is(err, application.ErrNewerSchema) {
		t.Fatal("newer saves are refused")
	}

	_ = os.WriteFile(filepath.Join(dir, "profile.json"), []byte(`{"schemaVersion":99}`), 0o600)
	if _, err := infrastructure.NewFileProfileRepository(dir).Load(); !errors.Is(err, application.ErrNewerSchema) {
		t.Fatal("newer profiles are refused")
	}

	if err := infrastructure.NewFileSaveRepository(dir).Delete(); err != nil {
		t.Fatal(err)
	}

	if err := infrastructure.NewFileSaveRepository(dir).Delete(); err != nil {
		t.Fatal("deleting a missing save is fine")
	}

	if records, err := infrastructure.NewFileHistoryRepository(t.TempDir()).List(); err != nil || len(records) != 0 {
		t.Fatal("empty history")
	}

	profile := application.NewProfile()
	profile.Bestiary["rat"] = &application.BestiaryEntry{Kills: 3, FirstKilledAt: "2026-01-01T00:00:00Z"}
	repository := infrastructure.NewFileProfileRepository(t.TempDir())

	if err := repository.Save(profile); err != nil {
		t.Fatal(err)
	}

	if loaded, err := repository.Load(); err != nil || loaded.Bestiary["rat"].Kills != 3 {
		t.Fatal("profile round trip")
	}

	if (infrastructure.SystemClock{}).Now().IsZero() {
		t.Fatal("clock must return now")
	}
}
