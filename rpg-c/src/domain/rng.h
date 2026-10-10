// mulberry32 PRNG shared by every implementation (docs/cross-language-parity.md).
// The engine must never use any other source of randomness. `uint32_t` arithmetic wraps, exactly like Math.imul.
#ifndef RPG_DOMAIN_RNG_H
#define RPG_DOMAIN_RNG_H

#include "domain/base.h"

typedef struct {
	uint32_t state;
} Rng;

Rng rng_new(int64_t seed);
uint32_t rng_next_u32(Rng *rng);
// Uniform integer in [minimum, maximum]; requires minimum <= maximum.
int64_t rng_roll(Rng *rng, int64_t minimum, int64_t maximum);
// Certain outcomes (percent <= 0 or >= 100) consume no number.
bool rng_chance(Rng *rng, int64_t percent);
// Index of the first weight whose cumulative sum reaches the roll.
size_t rng_weighted(Rng *rng, const int64_t *weights, size_t count);
// Index to pick from a list of `count` elements.
size_t rng_pick(Rng *rng, size_t count);

#endif
