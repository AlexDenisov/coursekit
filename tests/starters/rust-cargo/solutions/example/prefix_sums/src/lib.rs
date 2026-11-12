//! Sums of ranges of a fixed slice in O(1) each, after O(n) preprocessing.

pub struct PrefixSums {
    /// p[i] = a[0] + ... + a[i-1]
    p: Vec<i64>,
}

impl PrefixSums {
    /// O(n).
    pub fn new(a: &[i32]) -> Self {
        let mut p = vec![0i64];
        for &x in a {
            p.push(p.last().unwrap() + x as i64);
        }
        PrefixSums { p }
    }

    /// Number of elements of the original slice.
    pub fn len(&self) -> usize {
        self.p.len().saturating_sub(1)
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// a[l] + ... + a[r-1]; 0 when l == r. Panics unless l <= r <= len(). O(1).
    pub fn sum(&self, l: usize, r: usize) -> i64 {
        assert!(
            l <= r && r <= self.len(),
            "sum({l}, {r}) with len {}",
            self.len()
        );
        self.p[r] - self.p[l]
    }

    /// Optional. For a slice of non-negative values: the smallest r with
    /// sum(0, r) >= target, or len() + 1 if there is none. O(log n).
    pub fn lower_bound(&self, target: i64) -> usize {
        self.p.partition_point(|&x| x < target)
    }
}
