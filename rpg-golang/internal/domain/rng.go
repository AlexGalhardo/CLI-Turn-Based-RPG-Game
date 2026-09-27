// Package domain holds the pure game rules: PRNG, definitions, formulas and run entities.
package domain

import "fmt"

// Rng is the mulberry32 generator shared by the three implementations (docs/cross-language-parity.md).
// The engine must never use any other source of randomness.
type Rng struct {
	state uint32
}

// NewRng seeds the generator; the seed is taken modulo 2^32.
func NewRng(seed uint64) *Rng {
	return &Rng{state: uint32(seed)} //nolint:gosec // seeds are taken modulo 2^32 by design (docs/cross-language-parity.md).
}

// State returns the current internal state (stored in saves).
func (r *Rng) State() uint32 {
	return r.state
}

// NextU32 advances the generator. uint32 arithmetic wraps exactly like Math.imul / masking in the other languages.
func (r *Rng) NextU32() uint32 {
	r.state += 0x6D2B79F5
	t := r.state
	t = (t ^ (t >> 15)) * (t | 1)
	t ^= t + (t^(t>>7))*(t|61)

	return t ^ (t >> 14)
}

// Roll returns an integer in [minimum, maximum].
func (r *Rng) Roll(minimum, maximum int) int {
	if minimum > maximum {
		panic(fmt.Sprintf("invalid range [%d, %d]", minimum, maximum))
	}

	span := uint64(maximum - minimum + 1) //nolint:gosec // minimum <= maximum was checked, so the span is positive.

	return minimum + int(uint64(r.NextU32())%span) //nolint:gosec // the remainder is smaller than span, which fits an int.
}

// Chance rolls a percentage. Certain outcomes don't consume a number, so a 0% effect never shifts the sequence.
func (r *Rng) Chance(percent int) bool {
	if percent <= 0 {
		return false
	}

	if percent >= 100 {
		return true
	}

	return r.Roll(1, 100) <= percent
}

// Weighted returns the index chosen by weight.
func (r *Rng) Weighted(weights []int) int {
	total := 0
	for _, weight := range weights {
		total += weight
	}

	if total <= 0 {
		panic("weights must have a positive sum")
	}

	roll := r.Roll(1, total)

	cumulative := 0
	for index, weight := range weights {
		cumulative += weight
		if cumulative >= roll {
			return index
		}
	}

	panic("unreachable")
}

// Pick returns a random element of items.
func Pick[T any](r *Rng, items []T) T {
	if len(items) == 0 {
		panic("cannot pick from an empty sequence")
	}

	return items[r.Roll(0, len(items)-1)]
}
