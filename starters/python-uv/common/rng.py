"""A small deterministic random number generator (splitmix64).

Unlike `random`, its sequence is fixed by this file alone, so randomized tests
are reproducible everywhere, and the oracle scripts in tools/ can generate the
same inputs as the tests.

    rng = Rng(42)
    rng.below(10)        # 0..9, unbiased
    rng.between(-5, 5)   # -5..5, both ends included
    rng.uniform()        # [0, 1)
    rng.coin(0.25)       # True with probability 1/4
    rng.shuffle(xs)      # in place
"""

MASK = (1 << 64) - 1


class Rng:
    def __init__(self, seed: int) -> None:
        self._state = seed & MASK

    def next(self) -> int:
        self._state = (self._state + 0x9E3779B97F4A7C15) & MASK
        z = self._state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return z ^ (z >> 31)

    def below(self, n: int) -> int:
        """Uniform in [0, n), n > 0. Lemire's multiply-and-reject: no modulo bias."""
        if n <= 0:
            raise ValueError(f"below({n}): n must be positive")
        m = self.next() * n
        if (m & MASK) < n:
            threshold = (-n % (1 << 64)) % n
            while (m & MASK) < threshold:
                m = self.next() * n
        return m >> 64

    def between(self, lo: int, hi: int) -> int:
        """Uniform in [lo, hi], both included."""
        if lo > hi:
            raise ValueError(f"between({lo}, {hi}): lo > hi")
        return lo + self.below(hi - lo + 1)

    def uniform(self) -> float:
        """Uniform in [0, 1), 53 random bits."""
        return (self.next() >> 11) * 2.0**-53

    def coin(self, p: float = 0.5) -> bool:
        return self.uniform() < p

    def shuffle(self, xs: list) -> None:
        """Fisher-Yates, in place."""
        for i in range(len(xs), 1, -1):
            j = self.below(i)
            xs[i - 1], xs[j] = xs[j], xs[i - 1]
