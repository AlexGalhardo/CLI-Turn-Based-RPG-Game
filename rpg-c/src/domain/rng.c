#include "domain/rng.h"

Rng rng_new(int64_t seed) {
	// Converting to uint32_t is `seed mod 2^32`, also for negative seeds.
	Rng rng = {.state = (uint32_t)seed};
	return rng;
}

uint32_t rng_next_u32(Rng *rng) {
	rng->state += 0x6D2B79F5u;
	uint32_t t = rng->state;
	t = (t ^ (t >> 15)) * (t | 1u);
	t ^= t + (t ^ (t >> 7)) * (t | 61u);
	return t ^ (t >> 14);
}

int64_t rng_roll(Rng *rng, int64_t minimum, int64_t maximum) {
	if (minimum > maximum) {
		fatal("invalid range [%lld, %lld]", (long long)minimum, (long long)maximum);
	}
	return minimum + (int64_t)(rng_next_u32(rng) % (uint64_t)(maximum - minimum + 1));
}

bool rng_chance(Rng *rng, int64_t percent) {
	if (percent <= 0) {
		return false;
	}
	if (percent >= 100) {
		return true;
	}
	return rng_roll(rng, 1, 100) <= percent;
}

size_t rng_weighted(Rng *rng, const int64_t *weights, size_t count) {
	int64_t total = 0;
	for (size_t i = 0; i < count; i++) {
		total += weights[i];
	}
	if (total <= 0) {
		fatal("weights must have a positive sum");
	}
	int64_t roll = rng_roll(rng, 1, total);
	int64_t cumulative = 0;
	for (size_t i = 0; i < count; i++) {
		cumulative += weights[i];
		if (cumulative >= roll) {
			return i;
		}
	}
	fatal("unreachable: weighted roll out of range");
}

size_t rng_pick(Rng *rng, size_t count) {
	if (count == 0) {
		fatal("cannot pick from an empty sequence");
	}
	return (size_t)rng_roll(rng, 0, (int64_t)count - 1);
}
