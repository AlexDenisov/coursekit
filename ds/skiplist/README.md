# ds/skiplist: Skiplist with ranks

> **Summary.** A sorted set needs elements in score order, with O(log n)
> insert, delete, "what's my rank?" and "who is at rank 1000?". Redis uses a
> skiplist rather than a balanced tree: simpler to write, easy to walk
> backwards, and with one addition, *spans*, it answers rank queries too.
> `db/zsets` pairs yours with a dict. The surprising part: a coin flip per
> node is enough to keep it balanced.
>
> **Before:** nothing. **Next:** `db/zsets`.

## Levels

A sorted linked list where each node also gets a random height. Height 1 means
linked only at level 0, height 2 means also linked at level 1, and so on, with
P(height ≥ k+1) = 1/4 per extra level:

```
level 2:  H ─────────────────────────> 40 ───────────────> nil
level 1:  H ─────────> 20 ───────────> 40 ─────> 60 ─────> nil
level 0:  H ─> 10 ─> 20 ─> 30 ─> 40 ─> 50 ─> 60 ─> 70 ─> nil
```

A search starts at the top level and moves right while the next node is still
smaller, then drops one level. On average that's O(log n) steps. The order is
score, then member bytes, so equal scores still have a well-defined position.

## Spans: ranks for free

Each forward link stores how many level-0 nodes it jumps (its *span*). Summing
the spans of the links you cross during a search gives the rank. Following
links whose spans don't overshoot gives the node at a rank:

```
H ─(span 4)─> 40      rank of 40 = 4 - 1 = 3 (0-based)
```

Spans make insert and erase subtler. When you insert at some position, every
link that jumps over it gets span + 1, and the link you split is divided in
two. Redis's `zslInsert` tracks `rank[i]`, the rank of the last node before
the insertion point at level i, to compute both halves. `SkipList.MatchesAModel`
checks every span against level-0 distances after random edits, so an
off-by-one shows up quickly.

## The pitfalls

- Forgetting to bump the spans of *taller* levels that pass over a new node,
  or to shrink them on erase.
- Not lowering `level()` when the tallest node is removed.
- `backward` and `tail` (used for reverse iteration: ZREVRANGE) going stale.
- Ranks by walking level 0: correct, but O(n) per query
  (`SkipList.RankIsLogarithmic`).

## API

| Function | Contract |
|---|---|
| `insert(score, member)` | O(log n); member must be new |
| `erase(score, member)` | false if absent |
| `update_score(old, member, new)` | move an element |
| `rank(score, member)` / `at(rank)` | O(log n) via spans |
| `first_from(min, excl)` / `last_until(max, excl)` | range endpoints, for ZRANGE BYSCORE and ZCOUNT |
| `first()`, `last()`, `Node::next()`, `Node::backward` | iteration both ways |
| `random_level()` | geometric heights from the seeded Rng |

## Task

    bazel test //ds/skiplist:skiplist_test
