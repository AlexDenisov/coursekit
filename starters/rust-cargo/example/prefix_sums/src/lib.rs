//! Sums of ranges of a fixed slice in O(1) each, after O(n) preprocessing.

pub struct PrefixSums {
    /// p[i] = a[0] + ... + a[i-1]
    p: Vec<i64>,
}

impl PrefixSums {
    /// O(n).
    pub fn new(a: &[i32]) -> Self {
        // TODO(you): fill p with a.len() + 1 running totals, starting at 0.
        PrefixSums { p: Vec::new() }
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
        // TODO(you): one subtraction.
        0
    }

    /// Optional. For a slice of non-negative values: the smallest r with
    /// sum(0, r) >= target, or len() + 1 if there is none. O(log n).
    pub fn lower_bound(&self, target: i64) -> usize {
        // TODO(you, optional): p is non-decreasing; binary search it (slice::partition_point).
        0
    }
}
