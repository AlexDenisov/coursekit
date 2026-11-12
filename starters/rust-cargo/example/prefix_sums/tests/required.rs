use common::Rng;
use prefix_sums::PrefixSums;

#[test]
fn small_example_from_the_readme() {
    let p = PrefixSums::new(&[3, 1, 4, 1, 5]);
    assert_eq!(p.len(), 5);
    assert_eq!(p.sum(1, 4), 6);
    assert_eq!(p.sum(0, 5), 14);
}

#[test]
fn empty_range_is_zero() {
    let p = PrefixSums::new(&[7, 8]);
    assert_eq!(p.sum(0, 0), 0);
    assert_eq!(p.sum(2, 2), 0);
}

#[test]
fn empty_slice() {
    let p = PrefixSums::new(&[]);
    assert_eq!(p.len(), 0);
    assert_eq!(p.sum(0, 0), 0);
}

#[test]
fn large_values_do_not_overflow() {
    let a = vec![i32::MAX; 100_000];
    let p = PrefixSums::new(&a);
    assert_eq!(p.sum(0, a.len()), 100_000 * i32::MAX as i64);
}

// The oracle: a plain loop, written here so the test never trusts the code under test.
#[test]
#[allow(clippy::needless_range_loop)]
fn matches_brute_force_on_random_slices() {
    let mut rng = Rng::new(42);
    for round in 0..50 {
        let n = rng.below(40) as usize;
        let a: Vec<i32> = (0..n).map(|_| rng.between(-1000, 1000) as i32).collect();
        let p = PrefixSums::new(&a);
        for l in 0..=n {
            let mut want = 0i64;
            for r in l..=n {
                assert_eq!(p.sum(l, r), want, "l = {l}, r = {r}, round {round}");
                if r < n {
                    want += a[r] as i64;
                }
            }
        }
    }
}
