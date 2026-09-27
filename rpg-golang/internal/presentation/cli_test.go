package presentation_test

import (
	"errors"
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/version"
)

func TestParseCli(t *testing.T) {
	t.Parallel()

	result, err := presentation.ParseCli(nil)
	if err != nil || result.Exit || result.Options.Seed != nil || result.Options.NoAnim {
		t.Fatal("defaults")
	}

	result, err = presentation.ParseCli([]string{"--seed", "42", "--lang", "pt-BR", "--no-anim", "--data-dir", "x", "--simulate", "3"})
	if err != nil || *result.Options.Seed != 42 || result.Options.Lang != "pt-BR" || !result.Options.NoAnim || result.Options.DataDir != "x" || result.Options.Simulate != 3 {
		t.Fatalf("flags: %+v %v", result, err)
	}

	if result, _ := presentation.ParseCli([]string{"--version"}); result.Output != "rpg "+version.Version+" (golang)" {
		t.Fatal("version output")
	}

	if result, _ := presentation.ParseCli([]string{"--help"}); !strings.HasPrefix(result.Output, "usage: rpg") {
		t.Fatal("help output")
	}

	for _, args := range [][]string{{"--seed", "-1"}, {"--simulate", "0"}, {"--lang", "fr"}, {"--bogus"}} {
		if _, err := presentation.ParseCli(args); !errors.Is(err, presentation.ErrUsage) {
			t.Fatalf("%v must be a usage error", args)
		}
	}
}
