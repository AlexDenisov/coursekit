use prefix_sums::PrefixSums;

#[test]
fn lower_bound_finds_the_first_prefix_reaching_the_target() {
    let p = PrefixSums::new(&[3, 1, 4, 1, 5]); // prefixes 0 3 4 8 9 14
    assert_eq!(p.lower_bound(0), 0);
    assert_eq!(p.lower_bound(4), 2);
    assert_eq!(p.lower_bound(5), 3);
    assert_eq!(p.lower_bound(14), 5);
}

#[test]
fn lower_bound_unreachable_target_is_len_plus_one() {
    assert_eq!(PrefixSums::new(&[1, 2]).lower_bound(4), 3);
}
