# example/prefix_sums: Prefix sums

> **Summary.** A sample exercise that shows the shape of one; delete it when you
> bootstrap a real course. Prefix sums turn "sum of a range" from O(n) into
> O(1) after one O(n) pass.
>
> **Before:** nothing. **Next:** anything that answers many range queries.

## The idea

Store `p[i] = a[0] + ... + a[i-1]`, with `p[0] = 0`. Then the sum of
`a[l:r]` is `p[r] - p[l]`.

```
a = [3, 1, 4, 1, 5]
p = [0, 3, 4, 8, 9, 14]
sum of a[1:4] = p[4] - p[1] = 9 - 3 = 6     (1 + 4 + 1)
```

## The pitfall: n + 1 entries

`p` has one more entry than `a`. Without the leading 0, ranges that start
at 0 need a special case, and the empty array has nowhere to put its
(zero) sum. `test_empty_array` and `test_empty_range_is_zero` catch it.

## API

| Function | Contract |
|---|---|
| `PrefixSums(a)` | O(n) |
| `len(ps)` | number of elements of `a` |
| `ps.sum(l, r)` | sum of `a[l:r]`, O(1); requires `0 <= l <= r <= len(ps)`, else `IndexError` |
| `ps.lower_bound(target)` (optional) | smallest `r` with `sum(0, r) >= target`, for non-negative `a`; O(log n) |

## Task

    uv run pytest example/prefix_sums -m "not optional"
    uv run pytest example/prefix_sums -m optional
