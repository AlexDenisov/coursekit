# cmd/dispatch: Command table

> **Summary.** Between "a request was parsed" and "a reply was written" sits a
> table lookup. Redis keeps every command in one table (`server.c`,
> `commands.def`) with its name and arity, and checks the arity before the
> command runs. Every later command (`db/strings`, `db/lists`, ...) registers
> here, and every server (`server/hello_cli` and later) calls `dispatch`. The
> surprising part: even an error message has a byte budget, because it quotes
> what the user sent.
>
> **Before:** `resp/encoder`. **Next:** `db/strings`, `server/hello_cli`.

## Name, arity, function

```
{"get",  2, ...}    GET key                 exactly 2 arguments, counting the name
{"del", -2, ...}    DEL key [key ...]       at least 2
{"ping", -1, ...}   PING [message]          at least 1 (PING itself rejects a third)
```

`dispatch(args, reply)` runs one parsed request:

1. Look up `args[0]` ignoring case (`GET`, `get` and `GeT` are the same command).
2. Not found: reply with the unknown-command error.
3. Arity: for `N > 0` require `argc == N`; for `-N` require `argc >= N`.
   Otherwise reply `ERR wrong number of arguments for 'get' command`. The error
   uses the registered lowercase name, whatever the client typed.
4. Otherwise call the command with `{args, reply}`.

Exactly one reply per request. That's the rule that lets a client pipeline 100
requests and match up 100 replies in order.

Commands are `std::function`s, so a command can capture what it works on.
`db/strings` will register lambdas that capture its database:
`table.add({"get", 2, [&db](CommandContext& c) { ... }})`.

## The unknown-command message

```
FOO               ->  -ERR unknown command 'FOO'
FOO a b           ->  -ERR unknown command 'FOO', with args beginning with: 'a' 'b' 
```

Note the trailing space after each quoted arg. It's in Redis's output, so it's
in the tests.

## The pitfall: the message quotes user input

A client can send a command name of 10 MB, or 10,000 arguments. Redis bounds
the message:

- the name is cut to 128 bytes;
- args are added while the list built so far is shorter than 128 bytes, and
  each one is cut to `128 - (length so far)`.

```
FOO <100 a's> <100 b's> c
  'aaa...a' (103 bytes) -> 25 bytes left -> 'bbbbbbbbbbbbbbbbbbbbbbbbb' (131 bytes) -> stop, 'c' never shown
```

It's an off-by-one minefield, and `UnknownCommand.ArgsShareOneBudget` and
`UnknownCommand.ManyShortArgs` sit right on the boundaries. CR/LF in the
message are already handled by `append_error`
(`UnknownCommand.NewlinesInArgsAreNeutralised`).

## API

| Function | Contract |
|---|---|
| `CommandTable::add(spec)` | given; checks lowercase, unique, non-zero arity |
| `CommandTable::find(name)` | case-insensitive exact match, or `nullptr` |
| `CommandTable::dispatch(args, reply)` | lookup, arity, call; exactly one reply appended |
| `unknown_command_error(args)` | the message above, without the leading `-` |
| `arity_error(name)` | `ERR wrong number of arguments for '<name>' command` |
| `register_connection_commands(table)` | `PING [message]`, `ECHO message` |

## Task

    bazel test //cmd/dispatch:dispatch_test
