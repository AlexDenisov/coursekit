#include "cmd/dispatch/dispatch.h"

#include <algorithm>
#include <utility>

#include "common/check.h"
#include "resp/encoder/encoder.h"

namespace rd {

void CommandTable::add(CommandSpec spec) {
  RD_CHECK(!spec.name.empty());
  for (char c : spec.name) RD_CHECK(!(c >= 'A' && c <= 'Z'), "lowercase names: " << spec.name);
  RD_CHECK_NE(spec.arity, 0, spec.name);
  RD_CHECK(spec.fn != nullptr, spec.name);
  std::string key = spec.name;
  bool inserted = commands_.emplace(std::move(key), std::move(spec)).second;
  RD_CHECK(inserted, "command registered twice");
}

const CommandSpec* CommandTable::find(std::string_view name) const {
  std::string lower(name);
  for (char& c : lower) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  auto it = commands_.find(lower);
  return it == commands_.end() ? nullptr : &it->second;
}

void CommandTable::dispatch(std::span<const std::string> args, std::string& reply) const {
  RD_CHECK(!args.empty());
  const CommandSpec* spec = find(args[0]);
  if (spec == nullptr) {
    resp::append_error(reply, unknown_command_error(args));
    return;
  }
  auto argc = static_cast<int64_t>(args.size());
  if ((spec->arity > 0 && argc != spec->arity) || (spec->arity < 0 && argc < -spec->arity)) {
    resp::append_error(reply, arity_error(spec->name));
    return;
  }
  CommandContext ctx{args, reply};
  spec->fn(ctx);
}

std::string unknown_command_error(std::span<const std::string> args) {
  constexpr size_t kBudget = 128;
  std::string msg = "ERR unknown command '";
  msg += std::string_view(args[0]).substr(0, kBudget);
  msg += "'";
  if (args.size() == 1) return msg;
  std::string listed;
  for (size_t i = 1; i < args.size() && listed.size() < kBudget; ++i) {
    listed += '\'';
    listed += std::string_view(args[i]).substr(0, kBudget - listed.size() + 1);
    listed += "' ";
  }
  return msg + ", with args beginning with: " + listed;
}

std::string arity_error(std::string_view name) {
  return "ERR wrong number of arguments for '" + std::string(name) + "' command";
}

void register_connection_commands(CommandTable& table) {
  table.add({"ping", -1, [](CommandContext& c) {
               if (c.args.size() > 2) {
                 resp::append_error(c.reply, arity_error("ping"));
               } else if (c.args.size() == 2) {
                 resp::append_bulk_string(c.reply, c.args[1]);
               } else {
                 resp::append_simple_string(c.reply, "PONG");
               }
             }});
  table.add({"echo", 2, [](CommandContext& c) { resp::append_bulk_string(c.reply, c.args[1]); }});
}

}  // namespace rd
