package infrastructure_test

import (
	"errors"
	"io/fs"
	"maps"
	"testing"
	"testing/fstest"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/assets"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/infrastructure"
)

func TestLoadGameData(t *testing.T) {
	t.Parallel()

	data, err := infrastructure.LoadGameData(assets.Shared())
	if err != nil {
		t.Fatal(err)
	}

	if len(data.Monsters) < 100 || data.TierCount() != 10 || len(data.Vocations) != 3 {
		t.Fatal("content requirements not met")
	}

	for _, creature := range append(append(data.Monsters[:0:0], data.Monsters...), data.Bosses...) {
		if creature.IsBoss {
			if _, err := creature.Attack(creature.ChargeAttack); err != nil {
				t.Fatalf("%s: charge attack missing", creature.ID)
			}
		}
	}

	for _, vocation := range data.Vocations {
		if !data.HasItem(vocation.StarterWeapon) {
			t.Fatalf("%s: starter weapon missing", vocation.ID)
		}
	}

	if len(data.Items[0].Stats) == 0 {
		t.Fatal("item stats must be parsed in file order")
	}
}

func TestLoadGameData_Errors(t *testing.T) {
	t.Parallel()

	base := copyTree(t, assets.Shared())

	tests := []struct {
		name  string
		patch func(fstest.MapFS)
	}{
		{"missing file", func(fs fstest.MapFS) { delete(fs, "data/vocations.json") }},
		{"invalid json", func(fs fstest.MapFS) { fs["data/balance.json"] = &fstest.MapFile{Data: []byte("{")} }},
		{"no vocations", func(fs fstest.MapFS) { fs["data/vocations.json"] = &fstest.MapFile{Data: []byte(`{"vocations":[]}`)} }},
		{"bad item stats", func(fs fstest.MapFS) {
			fs["data/items.json"] = &fstest.MapFile{Data: []byte(`{"items":[{"id":"x","stats":{"attack":"a"}}]}`)}
		}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			shared := fstest.MapFS{}
			maps.Copy(shared, base)

			tt.patch(shared)

			if _, err := infrastructure.LoadGameData(shared); !errors.Is(err, infrastructure.ErrData) {
				t.Fatalf("expected ErrData, got %v", err)
			}
		})
	}
}

// copyTree copies a read-only FS into a MapFS so a test can patch individual files.
func copyTree(t *testing.T, source fs.FS) fstest.MapFS {
	t.Helper()

	target := fstest.MapFS{}

	err := fs.WalkDir(source, ".", func(path string, entry fs.DirEntry, walkErr error) error {
		if walkErr != nil || entry.IsDir() {
			return walkErr
		}

		content, err := fs.ReadFile(source, path)
		if err != nil {
			return err
		}

		target[path] = &fstest.MapFile{Data: content}

		return nil
	})
	if err != nil {
		t.Fatalf("copy shared tree: %v", err)
	}

	return target
}
