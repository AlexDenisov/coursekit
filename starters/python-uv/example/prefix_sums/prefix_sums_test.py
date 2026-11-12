import pytest

from common.rng import Rng
from example.prefix_sums.prefix_sums import PrefixSums


def test_small_example_from_the_readme():
    p = PrefixSums([3, 1, 4, 1, 5])
    assert len(p) == 5
    assert p.sum(1, 4) == 6
    assert p.sum(0, 5) == 14


def test_empty_range_is_zero():
    p = PrefixSums([7, 8])
    assert p.sum(0, 0) == 0
    assert p.sum(2, 2) == 0


def test_empty_array():
    p = PrefixSums([])
    assert len(p) == 0
    assert p.sum(0, 0) == 0


def test_out_of_range_raises():
    p = PrefixSums([1, 2])
    with pytest.raises(IndexError):
        p.sum(1, 3)


# The oracle: a plain loop, written here so the test never trusts the code under test.
def test_matches_brute_force_on_random_lists():
    rng = Rng(42)
    for round_ in range(50):
        a = [rng.between(-1000, 1000) for _ in range(rng.below(40))]
        p = PrefixSums(a)
        for l in range(len(a) + 1):  # noqa: E741
            want = 0
            for r in range(l, len(a) + 1):
                assert p.sum(l, r) == want, f"l = {l}, r = {r}, round {round_}"
                if r < len(a):
                    want += a[r]
