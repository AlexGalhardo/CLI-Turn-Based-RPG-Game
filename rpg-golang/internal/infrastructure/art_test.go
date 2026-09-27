package infrastructure_test

import (
	"errors"
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

func TestParseArt(t *testing.T) {
	t.Parallel()

	animations, err := infrastructure.ParseArt("@idle\n a\n%%\n b\n@hurt\n x\n")
	if err != nil || strings.Join(infrastructure.FrameFor(animations, "idle", 3), "") != " b" || strings.Join(infrastructure.FrameFor(animations, "attack", 0), "") != " a" {
		t.Fatalf("parse: %v %v", animations, err)
	}

	if infrastructure.FrameFor(infrastructure.Animations{}, "idle", 0) != nil {
		t.Fatal("no art means no frame")
	}

	for _, bad := range []string{"oops", "%%"} {
		if _, err := infrastructure.ParseArt(bad); !errors.Is(err, infrastructure.ErrArtFormat) {
			t.Fatalf("%q must fail", bad)
		}
	}

	data, _ := infrastructure.LoadGameData(assets.Shared())
	library := infrastructure.NewArtLibrary(assets.Shared())

	for i := range data.Monsters {
		if infrastructure.FrameFor(library.ForCreature(&data.Monsters[i]), "idle", 0) == nil {
			t.Fatalf("%s has no art", data.Monsters[i].ID)
		}
	}

	for i := range data.Bosses {
		if infrastructure.FrameFor(library.ForCreature(&data.Bosses[i]), "idle", 0) == nil {
			t.Fatalf("%s has no art", data.Bosses[i].ID)
		}
	}

	if len(library.LoadFile("families", "missing")) != 0 {
		t.Fatal("missing art is empty")
	}
}
