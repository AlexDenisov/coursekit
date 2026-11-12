# example/prefix_sums: Prefix sums

> **Summary.** A sample exercise that shows the shape of one; delete it when you
> bootstrap a real course. Prefix sums turn "sum of a range" from O(n) into
> O(1) after one O(n) pass. The surprising bit: the sum of a hundred thousand
> `i32`s doesn't fit in an `i32` (and debug builds panic when it overflows).
>
> **Before:** nothing. **Next:** anything that answers many range queries.

## The idea

Store `p[i] = a[0] + ... + a[i-1]`, with `p[0] = 0`. Then the sum of
`a[l..r]` is `p[r] - p[l]`.

```
a = [3, 1, 4, 1, 5]
p = [0, 3, 4, 8, 9, 14]
sum of a[1..4] = p[4] - p[1] = 9 - 3 = 6     (1 + 4 + 1)
```

## The pitfall: overflow

`a` holds `i32`, but `p` must be `i64`: 70 000 values of `i32::MAX` already
overflow 32 bits. The test `large_values_do_not_overflow` catches it.

## API

| Function | Contract |
|---|---|
| `PrefixSums::new(a: &[i32])` | O(n) |
| `len()` | number of elements of `a` |
| `sum(l, r)` | sum of `a[l..r]`, O(1); panics unless `l <= r <= len()` |
| `lower_bound(target)` (optional) | smallest `r` with `sum(0, r) >= target`, for non-negative `a`; O(log n) |

## Task

    cargo test -p prefix_sums --test required
    cargo test -p prefix_sums --test optional
