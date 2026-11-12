#pragma once

// cmd/dispatch: the command table. The spec is this file; the story is README.md.

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rd {

// What a command implementation sees.
struct CommandContext {
  // args[0] is the command name exactly as the client sent it ("get", "GET", "GeT").
  std::span<const std::string> args;
  // The client's reply buffer. A command appends exactly one reply (which may be an
  // array of many) with the resp/encoder functions, and never clears it.
  std::string& reply;
};

using CommandFn = std::function<void(CommandContext&)>;

struct CommandSpec {
  // Lowercase, as it appears in error messages: "get".
  std::string name;
  // Redis's convention, counting the name itself: N > 0 means exactly N arguments
  // ("get" has arity 2: GET key); -N means at least N ("del" has -2: DEL key [key ...]).
  int arity = 0;
  CommandFn fn;
};

class CommandTable {
 public:
  // Registers a command. Checks that the name is non-empty, lowercase and new, the
  // arity non-zero and fn set. Given (already implemented).
  void add(CommandSpec spec);

  // The command called `name`, ignoring ASCII case, or nullptr. O(|name|) on average.
  const CommandSpec* find(std::string_view name) const;

  size_t size() const { return commands_.size(); }

  // Runs one request and appends exactly one reply to `reply`:
  //   - unknown name: the error from unknown_command_error(args);
  //   - wrong number of arguments for the arity: arity_error(spec.name);
  //   - otherwise the command's fn.
  // Precondition (checked): args is non-empty. O(|args[0]|) + the command's own cost.
  void dispatch(std::span<const std::string> args, std::string& reply) const;

 private:
  std::unordered_map<std::string, CommandSpec> commands_;  // key: the lowercase name
};

// "ERR unknown command '<name>'" when args has only the name, otherwise
// "ERR unknown command '<name>', with args beginning with: '<a1>' '<a2>' ... " (note
// the space after each quoted arg). <name> is cut to 128 bytes. The quoted args are
// added while the list built so far (quotes and spaces included) is shorter than 128
// bytes, and each is cut to 128 minus that length. O(1) in the number of args.
std::string unknown_command_error(std::span<const std::string> args);

// "ERR wrong number of arguments for '<name>' command". Commands with their own
// argument rules (PING, MSET) use it too.
std::string arity_error(std::string_view name);

// PING [message]  ->  +PONG, or the message as a bulk string; more args: arity error.
// ECHO message    ->  the message as a bulk string.
void register_connection_commands(CommandTable& table);

}  // namespace rd
