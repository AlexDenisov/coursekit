#include "cmd/dispatch/dispatch.h"

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
  // TODO(you): lowercase a copy of name and look it up.
  return nullptr;
}

void CommandTable::dispatch(std::span<const std::string> args, std::string& reply) const {
  RD_CHECK(!args.empty());
  // TODO(you): find, check the arity (both signs), then call fn with a CommandContext.
}

std::string unknown_command_error(std::span<const std::string> args) {
  // TODO(you): build the message piece by piece; watch the 128-byte budget.
  return "";
}

std::string arity_error(std::string_view name) {
  // TODO(you): one concatenation.
  return "";
}

void register_connection_commands(CommandTable& table) {
  // TODO(you): table.add({"ping", -1, [](CommandContext& c) { ... }}); same for echo.
}

}  // namespace rd
