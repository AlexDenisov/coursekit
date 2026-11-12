"""Sums of ranges of a fixed list in O(1) each, after O(n) preprocessing."""

from bisect import bisect_left
from collections.abc import Sequence


class PrefixSums:
    def __init__(self, a: Sequence[int]) -> None:
        self._p: list[int] = [0]
        for x in a:
            self._p.append(self._p[-1] + x)

    def __len__(self) -> int:
        return max(0, len(self._p) - 1)

    def sum(self, l: int, r: int) -> int:  # noqa: E741
        if not 0 <= l <= r <= len(self):
            raise IndexError(f"sum({l}, {r}) with len {len(self)}")
        return self._p[r] - self._p[l]

    def lower_bound(self, target: int) -> int:
        return bisect_left(self._p, target)
