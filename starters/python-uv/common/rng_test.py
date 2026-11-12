from common.rng import Rng


def test_same_seed_same_numbers():
    a, b, c = Rng(42), Rng(42), Rng(43)
    for _ in range(100):
        x = a.next()
        assert x == b.next()
        assert x != c.next()


def test_known_sequence():
    # splitmix64 reference values for seed 0; the C++ and Rust starters produce the same.
    r = Rng(0)
    assert r.next() == 0xE220A8397B1DCDAF
    assert r.next() == 0x6E789E6AA1B965F4


def test_between_includes_both_ends():
    r = Rng(2)
    seen = {r.between(-2, 2) for _ in range(1000)}
    assert seen == {-2, -1, 0, 1, 2}


def test_shuffle_is_a_permutation():
    r = Rng(4)
    xs = list(range(50))
    r.shuffle(xs)
    assert xs != list(range(50))
    assert sorted(xs) == list(range(50))
