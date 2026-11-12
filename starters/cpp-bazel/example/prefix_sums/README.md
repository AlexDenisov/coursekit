# example/prefix_sums: Prefix sums

> **Summary.** A sample exercise that shows the shape of one; delete it when you
> bootstrap a real course. Prefix sums turn "sum of a range" from O(n) into
> O(1) after one O(n) pass. The surprising bit: the sum of a million
> `int32_t`s doesn't fit in an `int32_t`.
>
> **Before:** nothing. **Next:** anything that answers many range queries.

## The idea

Store `p[i] = a[0] + ... + a[i-1]`, with `p[0] = 0`. Then the sum of
`a[l..r)` is `p[r] - p[l]`.

```
a = [3, 1, 4, 1, 5]
p = [0, 3, 4, 8, 9, 14]
sum of a[1..4) = p[4] - p[1] = 9 - 3 = 6     (1 + 4 + 1)
```

## The pitfall: overflow

`a` holds `int32_t`, but `p` must be `int64_t`: 70 000 values of
`INT32_MAX` already overflow 32 bits. The test
`PrefixSums.LargeValuesDoNotOverflow` catches it.

## API

| Function | Contract |
|---|---|
| `PrefixSums(std::span<const int32_t> a)` | O(n) |
| `sum(l, r)` | sum of `a[l..r)`, O(1); requires `l <= r <= size()` |
| `lower_bound(target)` (optional) | smallest `r` with `sum(0, r) >= target`, for non-negative `a`; O(log n) |

## Task

    bazel test //example/prefix_sums:prefix_sums_test
    bazel test //example/prefix_sums:optional_test
