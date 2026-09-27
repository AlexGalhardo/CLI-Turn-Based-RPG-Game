package domain_test

import (
	"testing"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

func TestNewRng(t *testing.T) {
	t.Parallel()

	if domain.NewRng(1<<32+42).NextU32() != domain.NewRng(42).NextU32() {
		t.Fatal("seed must be reduced modulo 2^32")
	}
}

func TestRng_State(t *testing.T) {
	t.Parallel()

	rng := domain.NewRng(7)
	rng.NextU32()
	clone := domain.NewRng(uint64(rng.State()))

	for range 5 {
		if clone.NextU32() != rng.NextU32() {
			t.Fatal("restoring the state must continue the sequence")
		}
	}
}

func TestRng_Roll(t *testing.T) {
	t.Parallel()

	rng := domain.NewRng(1)
	seen := map[int]bool{}

	for range 500 {
		value := rng.Roll(3, 5)
		if value < 3 || value > 5 {
			t.Fatalf("roll out of range: %d", value)
		}

		seen[value] = true
	}

	if len(seen) != 3 {
		t.Fatalf("expected every value in [3, 5], got %v", seen)
	}

	defer func() {
		if recover() == nil {
			t.Fatal("inverted range must panic")
		}
	}()

	rng.Roll(5, 4)
}

func TestRng_Chance(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name     string
		percent  int
		expected bool
	}{
		{"zero never happens", 0, false},
		{"negative never happens", -5, false},
		{"hundred always happens", 100, true},
		{"above hundred always happens", 150, true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			rng := domain.NewRng(9)
			before := rng.State()

			if rng.Chance(tt.percent) != tt.expected {
				t.Fatalf("Chance(%d) != %v", tt.percent, tt.expected)
			}

			if rng.State() != before {
				t.Fatal("certain outcomes must not consume randomness")
			}
		})
	}

	t.Run("uncertain chance consumes one roll", func(t *testing.T) {
		t.Parallel()

		rng, reference := domain.NewRng(9), domain.NewRng(9)
		if rng.Chance(50) != (reference.Roll(1, 100) <= 50) || rng.State() != reference.State() {
			t.Fatal("chance must be roll(1, 100) <= percent")
		}
	})
}

func TestRng_Weighted(t *testing.T) {
	t.Parallel()

	rng := domain.NewRng(3)
	for range 50 {
		if rng.Weighted([]int{0, 5, 0}) != 1 {
			t.Fatal("zero weights must never be picked")
		}
	}

	defer func() {
		if recover() == nil {
			t.Fatal("empty total must panic")
		}
	}()

	rng.Weighted([]int{0, 0})
}

func TestPick(t *testing.T) {
	t.Parallel()

	rng := domain.NewRng(5)
	if domain.Pick(rng, []string{"a"}) != "a" {
		t.Fatal("single element must be picked")
	}

	defer func() {
		if recover() == nil {
			t.Fatal("empty slice must panic")
		}
	}()

	domain.Pick(rng, []string{})
}
