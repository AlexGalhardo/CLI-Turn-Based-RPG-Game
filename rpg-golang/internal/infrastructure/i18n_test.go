package infrastructure_test

import (
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

func TestTranslator(t *testing.T) {
	t.Parallel()

	english, err := infrastructure.NewTranslator(assets.Shared(), "en")
	if err != nil {
		t.Fatal(err)
	}

	portuguese, err := infrastructure.NewTranslator(assets.Shared(), "pt-BR")
	if err != nil {
		t.Fatal(err)
	}

	if english.T("event.gold_looted", map[string]any{"amount": 5}) != "You looted 5 gold." ||
		portuguese.T("event.gold_looted", map[string]any{"amount": 5}) != "Você saqueou 5 de ouro." {
		t.Fatal("translations wrong")
	}

	if english.T("missing.key", nil) != "missing.key" || english.T("event.gold_looted", nil) != "You looted {amount} gold." || !portuguese.Has("menu.quit") {
		t.Fatal("fallbacks wrong")
	}

	if _, err := infrastructure.NewTranslator(assets.Shared(), "fr"); err == nil {
		t.Fatal("unsupported locale must fail")
	}
}
