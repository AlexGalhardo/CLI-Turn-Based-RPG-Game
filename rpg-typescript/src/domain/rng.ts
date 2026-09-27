/** mulberry32 PRNG shared by the three implementations (docs/cross-language-parity.md). */
export class Rng {
	#state: number;

	constructor(seed: number) {
		this.#state = seed >>> 0;
	}

	get state(): number {
		return this.#state;
	}

	nextU32(): number {
		this.#state = (this.#state + 0x6d2b79f5) >>> 0;
		let t = this.#state;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return (t ^ (t >>> 14)) >>> 0;
	}

	roll(minimum: number, maximum: number): number {
		if (minimum > maximum) {
			throw new RangeError(`invalid range [${minimum}, ${maximum}]`);
		}
		return minimum + (this.nextU32() % (maximum - minimum + 1));
	}

	chance(percent: number): boolean {
		// Certain outcomes don't consume a number, so adding a 0% effect never shifts the sequence.
		if (percent <= 0) return false;
		if (percent >= 100) return true;
		return this.roll(1, 100) <= percent;
	}

	weighted(weights: readonly number[]): number {
		const total = weights.reduce((sum, weight) => sum + weight, 0);
		if (total <= 0) {
			throw new RangeError("weights must have a positive sum");
		}
		const roll = this.roll(1, total);
		let cumulative = 0;
		for (const [index, weight] of weights.entries()) {
			cumulative += weight;
			if (cumulative >= roll) return index;
		}
		throw new Error("unreachable");
	}

	pick<T>(items: readonly T[]): T {
		if (items.length === 0) {
			throw new RangeError("cannot pick from an empty sequence");
		}
		return items[this.roll(0, items.length - 1)] as T;
	}
}
