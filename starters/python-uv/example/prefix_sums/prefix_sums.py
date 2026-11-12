"""Sums of ranges of a fixed list in O(1) each, after O(n) preprocessing."""

from collections.abc import Sequence


class PrefixSums:
    def __init__(self, a: Sequence[int]) -> None:
        """O(n)."""
        self._p: list[int] = []  # _p[i] = a[0] + ... + a[i-1]
        # TODO(you): fill self._p with len(a) + 1 running totals, starting at 0.

    def __len__(self) -> int:
        """Number of elements of the original list."""
        return max(0, len(self._p) - 1)

    def sum(self, l: int, r: int) -> int:  # noqa: E741
        """a[l] + ... + a[r-1]; 0 when l == r. Requires 0 <= l <= r <= len(self),
        else IndexError. O(1)."""
        if not 0 <= l <= r <= len(self):
            raise IndexError(f"sum({l}, {r}) with len {len(self)}")
        # TODO(you): one subtraction.
        return 0

    def lower_bound(self, target: int) -> int:
        """Optional. For a list of non-negative values: the smallest r with
        sum(0, r) >= target, or len(self) + 1 if there is none. O(log n)."""
        # TODO(you, optional): _p is non-decreasing; binary search it (or use bisect).
        return 0
