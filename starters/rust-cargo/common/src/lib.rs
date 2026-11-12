//! Shared helpers every exercise may use. Keep them small and obviously correct:
//! tests rely on them, and they must never call the code under test.

/// A small deterministic random number generator (splitmix64). Its sequence is
/// fixed by this file alone, so randomized tests are reproducible everywhere,
/// and oracle scripts can generate the same inputs.
///
/// ```
/// let mut rng = common::Rng::new(42);
/// let x = rng.below(10);          // 0..9, unbiased
/// let y = rng.between(-5, 5);     // -5..=5
/// let mut v = vec![1, 2, 3];
/// rng.shuffle(&mut v);
/// ```
pub struct Rng {
    state: u64,
}

impl Rng {
    pub fn new(seed: u64) -> Self {
        Rng { state: seed }
    }

    pub fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Uniform in [0, n), n > 0. Lemire's multiply-and-reject: no modulo bias.
    pub fn below(&mut self, n: u64) -> u64 {
        assert!(n > 0, "below(0)");
        let mut m = self.next_u64() as u128 * n as u128;
        if (m as u64) < n {
            let threshold = n.wrapping_neg() % n;
            while (m as u64) < threshold {
                m = self.next_u64() as u128 * n as u128;
            }
        }
        (m >> 64) as u64
    }

    /// Uniform in [lo, hi], both included.
    pub fn between(&mut self, lo: i64, hi: i64) -> i64 {
        assert!(lo <= hi, "between({lo}, {hi})");
        let span = hi.wrapping_sub(lo) as u64;
        let r = if span == u64::MAX {
            self.next_u64()
        } else {
            self.below(span + 1)
        };
        lo.wrapping_add(r as i64)
    }

    /// Uniform in [0, 1), 53 random bits.
    pub fn uniform(&mut self) -> f64 {
        (self.next_u64() >> 11) as f64 * (1.0 / (1u64 << 53) as f64)
    }

    pub fn coin(&mut self, p: f64) -> bool {
        self.uniform() < p
    }

    /// Fisher-Yates.
    pub fn shuffle<T>(&mut self, v: &mut [T]) {
        for i in (2..=v.len()).rev() {
            let j = self.below(i as u64) as usize;
            v.swap(i - 1, j);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Rng;

    #[test]
    fn known_sequence() {
        // splitmix64 reference values for seed 0; the C++ and Python starters produce the same.
        let mut r = Rng::new(0);
        assert_eq!(r.next_u64(), 0xE220_A839_7B1D_CDAF);
        assert_eq!(r.next_u64(), 0x6E78_9E6A_A1B9_65F4);
    }

    #[test]
    fn between_includes_both_ends() {
        let mut r = Rng::new(2);
        let mut seen = [false; 5];
        for _ in 0..1000 {
            seen[(r.between(-2, 2) + 2) as usize] = true;
        }
        assert!(seen.iter().all(|&s| s));
        r.between(i64::MIN, i64::MAX); // full range doesn't overflow
    }

    #[test]
    fn shuffle_is_a_permutation() {
        let mut r = Rng::new(4);
        let mut v: Vec<i32> = (0..50).collect();
        r.shuffle(&mut v);
        assert_ne!(v, (0..50).collect::<Vec<_>>());
        v.sort();
        assert_eq!(v, (0..50).collect::<Vec<_>>());
    }
}
