package infrastructure_test

import (
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

func TestResolveDataDir(t *testing.T) {
	if infrastructure.ResolveDataDir("custom") != "custom" {
		t.Fatal("flag wins")
	}

	t.Setenv(infrastructure.DataDirEnv, "from-env")

	if infrastructure.ResolveDataDir("") != "from-env" {
		t.Fatal("env var is used")
	}

	t.Setenv(infrastructure.DataDirEnv, "")

	if !strings.HasSuffix(infrastructure.ResolveDataDir(""), ".cli-turn-based-rpg") {
		t.Fatal("default under home")
	}
}
