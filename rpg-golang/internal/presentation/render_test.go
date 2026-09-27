package presentation_test

import (
	"strings"
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/presentation"
)

func TestBar(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name                   string
		current, maximum, want int
	}{
		{"empty", 0, 100, 0},
		{"alive shows one cell", 1, 100, 1},
		{"full", 100, 100, 10},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if got := strings.Count(presentation.Bar(tt.current, tt.maximum, 10), "█"); got != tt.want {
				t.Fatalf("filled = %d, want %d", got, tt.want)
			}
		})
	}

	if presentation.Bar(5, 0, 4) != strings.Repeat("░", 4) {
		t.Fatal("zero maximum is empty")
	}
}

func TestHPColor(t *testing.T) {
	t.Parallel()

	if presentation.HPColor(60, 100) == presentation.HPColor(30, 100) || presentation.HPColor(30, 100) == presentation.HPColor(10, 100) {
		t.Fatal("three colour bands expected")
	}
}

func TestListKey(t *testing.T) {
	t.Parallel()

	if presentation.ListKey(0) != "1" || presentation.ListKey(9) != "a" || presentation.ListIndex("a") != 9 ||
		presentation.ListIndex("!") != -1 || presentation.ListIndex("ab") != -1 {
		t.Fatal("list keys are 1-9 then a-z")
	}
}
