# server/messaging_redis: ★ Messaging Redis

> **Summary.** The Messaging milestone: clients talking to clients through
> your server. A chat over `SUBSCRIBE`/`PUBLISH`, a job queue where workers
> park in `BRPOP` and get exactly one job each, and a counter that many
> clients increment with `WATCH`/`MULTI`/`EXEC` without ever losing an update.
> The three modules are done; this exercise wires them into the request path
> of every client. The surprising part: the hard bugs are about *whose* turn
> it is, not *what* happens.
>
> **Before:** `server/mini_redis`, `msg/pubsub`, `msg/transactions`,
> `msg/blocking`. **Next:** you've finished the course. See
> `docs/future_topics.md` for where to go from here.

## The request path

Every request of every client goes through one runner:

```
pubsub_gate      subscribed? only (UN)SUBSCRIBE, PING, ... may run
transactions     MULTI/EXEC/WATCH; queue everything else while in MULTI
dispatch         the command itself
serve_ready      after *every* command: hand elements to clients parked in BLPOP
```

And at the edges of a client's life:

- **blocking** calls `Client::block()`: the client's further requests wait
  in its query buffer. **Unblocking** calls `unblock()` and `process_input()`
  so they run, in order (`Messaging.BlockedClientKeepsItsOrder`: a PING sent
  right after BLPOP answers only after BLPOP does);
- **closing** (`EventServer::set_close_hook`): all three modules forget the
  session, or a later PUBLISH or RPUSH writes into a freed `Client`.

## Order and fairness

- Messages to a subscriber are appended to its reply buffer by *someone
  else's* command. Your EventServer's before-sleep flush sends them.
- `BLPOP` inside `EXEC` must not block: a transaction can't pause halfway.
  The blocking module asks `Transactions::executing()`.
- Waiters are served oldest first, one element each. The job queue test runs
  4 workers against 400 jobs and checks each job is processed exactly once.

## Try it

```sh
bazel run -c opt //server/messaging_redis -- 6380
redis-cli -p 6380 SUBSCRIBE chat                 # terminal 1
redis-cli -p 6380 PUBLISH chat "hello"           # terminal 2
redis-cli -p 6380 BRPOP jobs 0                   # terminal 3: waits...
redis-cli -p 6380 LPUSH jobs build-42            # ...until this
```

## API

| Function | Contract |
|---|---|
| `MessagingRedis(clock, options)` | the wiring described above |
| `listen`, `run`, `stop`, `base()`, `main.cc` | given |

## Task

    bazel test //server/messaging_redis:messaging_redis_test --test_output=all
