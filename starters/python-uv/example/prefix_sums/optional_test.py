import pytest

from example.prefix_sums.prefix_sums import PrefixSums

pytestmark = pytest.mark.optional


def test_lower_bound_finds_the_first_prefix_reaching_the_target():
    p = PrefixSums([3, 1, 4, 1, 5])  # prefixes 0 3 4 8 9 14
    assert p.lower_bound(0) == 0
    assert p.lower_bound(4) == 2
    assert p.lower_bound(5) == 3
    assert p.lower_bound(14) == 5


def test_lower_bound_unreachable_target_is_len_plus_one():
    assert PrefixSums([1, 2]).lower_bound(4) == 3
