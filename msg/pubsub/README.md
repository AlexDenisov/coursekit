# msg/pubsub: Pub/Sub

> **Summary.** Clients talking to clients: `SUBSCRIBE news` turns a
> connection into a listener, and `PUBLISH news hello` from any other
> connection delivers the message to every listener *now*. Nothing is stored:
> a listener that isn't connected misses it. `server/messaging_redis` runs a
> chat on it. The surprising part: a reply goes to a client that didn't send
> anything, so the reply buffer stops being "the answer to your last
> request".
>
> **Before:** `cmd/dispatch` (Session), `db/glob` (patterns), `net/client`
> (`push`, output buffers). **Next:** `server/messaging_redis`.

## The protocol (recorded from Redis 8.10)

```
A: SUBSCRIBE news sports   A ← [subscribe, news, 1] [subscribe, sports, 2]
B: PUBLISH news hello      A ← [message, news, hello]          B ← :1 (receivers)
A: PSUBSCRIBE news.*       A ← [psubscribe, news.*, 3]
B: PUBLISH news.tech x     A ← [pmessage, news.*, news.tech, x]
A: GET k                   A ← -ERR Can't execute 'get': only (P|S)SUBSCRIBE / ... in this context
A: PING                    A ← [pong, ""]                 (not +PONG: subscriber mode)
A: UNSUBSCRIBE             A ← [unsubscribe, sports, 1] ...  one confirmation per channel
```

The number in each confirmation is how many subscriptions (channels plus
patterns) the client has left. When it reaches 0, the client is back in
normal mode.

## Two indexes

- channel → set of sessions: `PUBLISH` looks up one entry.
- per session → its channels and patterns: `UNSUBSCRIBE` without arguments,
  and cleanup when the client disconnects.

Patterns can't be indexed: `PUBLISH` checks every pattern with
`glob_match`. That's why Redis's docs warn that many pattern subscriptions
make every publish slower.

## The pitfalls

- **Disconnects.** A subscriber that goes away without unsubscribing must be
  removed from every index (`forget`), or the next PUBLISH pushes into freed
  memory (`PubSubTest.ForgetDropsEverything`). That's what
  `EventServer::set_close_hook` is for.
- **Empty channels.** Remove a channel's entry when its last subscriber
  leaves, or `PUBSUB CHANNELS` lists ghosts.
- **Slow subscribers.** A subscriber that doesn't read fills its reply buffer
  with every message published, without bound. Redis disconnects pub/sub
  clients whose output buffer passes 32 MB (`client-output-buffer-limit
  pubsub 32mb 8mb 60`). Your server keeps them, and that's a good follow-up
  exercise.

## API

| Function | Contract |
|---|---|
| `PubSub::subscribe/psubscribe/unsubscribe/punsubscribe(s, names, reply)` | confirmations into `reply` |
| `PubSub::publish(channel, message)` | pushes; returns receivers |
| `subscriptions(s)`, `forget(s)`, `channels(pattern)`, `numsub`, `numpat` | |
| `register_pubsub_commands(table, pubsub)` | SUBSCRIBE ... PUBLISH PUBSUB |
| `pubsub_gate(pubsub, s, args, reply)` | subscriber-mode rules |

## Task

    bazel test //msg/pubsub:pubsub_test
