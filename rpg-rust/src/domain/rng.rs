//! mulberry32 PRNG shared by every implementation (docs/cross-language-parity.md).

/// Deterministic 32-bit generator. The engine must never use any other source of randomness.
///
/// `u32` arithmetic with `wrapping_*` gives the modulo-2³² behaviour that Python emulates with `& 0xFFFFFFFF`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Rng {
	state: u32,
}

impl Rng {
	/// The seed is reduced modulo 2³².
	pub fn new(seed: u64) -> Rng {
		Rng { state: (seed & 0xFFFF_FFFF) as u32 }
	}

	pub fn state(&self) -> u32 {
		self.state
	}

	pub fn next_u32(&mut self) -> u32 {
		self.state = self.state.wrapping_add(0x6D2B_79F5);
		let mut t = self.state;
		t = (t ^ (t >> 15)).wrapping_mul(t | 1);
		// 0x3D = 61, written in hex because it is a bit mask.
		t ^= t.wrapping_add((t ^ (t >> 7)).wrapping_mul(t | 0x3D));
		t ^ (t >> 14)
	}

	/// Uniform integer in `[minimum, maximum]`. An inverted range is a data bug, so it panics.
	pub fn roll(&mut self, minimum: i64, maximum: i64) -> i64 {
		assert!(minimum <= maximum, "invalid range [{minimum}, {maximum}]");
		minimum + i64::from(self.next_u32()) % (maximum - minimum + 1)
	}

	pub fn chance(&mut self, percent: i64) -> bool {
		// Certain outcomes don't consume a number, so adding a 0% effect never shifts the sequence.
		if percent <= 0 {
			return false;
		}
		if percent >= 100 {
			return true;
		}
		self.roll(1, 100) <= percent
	}

	pub fn weighted(&mut self, weights: &[i64]) -> usize {
		let total: i64 = weights.iter().sum();
		assert!(total > 0, "weights must have a positive sum");
		let roll = self.roll(1, total);
		let mut cumulative = 0;
		for (index, weight) in weights.iter().enumerate() {
			cumulative += weight;
			if cumulative >= roll {
				return index;
			}
		}
		unreachable!("cumulative weight always reaches the roll")
	}

	pub fn pick<'a, T>(&mut self, items: &'a [T]) -> &'a T {
		assert!(!items.is_empty(), "cannot pick from an empty sequence");
		&items[self.roll(0, items.len() as i64 - 1) as usize]
	}
}

#[cfg(test)]
mod tests {
	use super::Rng;

	#[test]
	fn seed_is_reduced_modulo_2_32() {
		assert_eq!(Rng::new((1 << 32) + 42).next_u32(), Rng::new(42).next_u32());
	}

	#[test]
	fn state_round_trip_continues_sequence() {
		let mut rng = Rng::new(7);
		rng.next_u32();
		let mut clone = Rng::new(u64::from(rng.state()));
		let a: Vec<u32> = (0..5).map(|_| clone.next_u32()).collect();
		let b: Vec<u32> = (0..5).map(|_| rng.next_u32()).collect();
		assert_eq!(a, b);
	}

	#[test]
	fn roll_is_inclusive_and_bounded() {
		let mut rng = Rng::new(1);
		let mut values: Vec<i64> = (0..500).map(|_| rng.roll(3, 5)).collect();
		values.sort_unstable();
		values.dedup();
		assert_eq!(values, vec![3, 4, 5]);
	}

	#[test]
	#[should_panic(expected = "invalid range")]
	fn roll_rejects_inverted_range() {
		Rng::new(1).roll(5, 4);
	}

	#[test]
	fn certain_chances_do_not_consume() {
		for (percent, expected) in [(0, false), (-5, false), (100, true), (150, true)] {
			let mut rng = Rng::new(9);
			let state = rng.state();
			assert_eq!(rng.chance(percent), expected);
			assert_eq!(rng.state(), state);
		}
	}

	#[test]
	fn chance_consumes_one_number() {
		let mut rng = Rng::new(9);
		let mut reference = Rng::new(9);
		let result = rng.chance(50);
		assert_eq!(result, reference.roll(1, 100) <= 50);
		assert_eq!(rng.state(), reference.state());
	}

	#[test]
	fn weighted_respects_zero_weights() {
		let mut rng = Rng::new(3);
		assert!((0..50).all(|_| rng.weighted(&[0, 5, 0]) == 1));
	}

	#[test]
	#[should_panic(expected = "positive")]
	fn weighted_rejects_empty_total() {
		Rng::new(3).weighted(&[0, 0]);
	}

	#[test]
	fn pick() {
		let mut rng = Rng::new(5);
		assert_eq!(*rng.pick(&["a"]), "a");
	}

	#[test]
	#[should_panic(expected = "empty")]
	fn pick_rejects_empty() {
		let empty: [&str; 0] = [];
		Rng::new(5).pick(&empty);
	}
}
