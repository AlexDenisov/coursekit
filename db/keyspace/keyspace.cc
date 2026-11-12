#include "db/keyspace/keyspace.h"

#include <limits>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

std::string_view StringValue::encoding() const {
  // TODO(you): the rule in keyspace.h.
  return "";
}

bool Keyspace::expire_if_needed(std::string_view key) {
  // TODO(you): if key's TTL is <= now, remove it from entries_ and expires_.
  return false;
}

void Keyspace::modified(std::string_view key) {
  // TODO(you): dirty_ + 1, then the hook if set.
}

Value* Keyspace::find(std::string_view key) {
  // TODO(you): expire_if_needed, look up, touch lru.
  return nullptr;
}

Value* Keyspace::find_for_write(std::string_view key) {
  // TODO(you): find, and modified() if found.
  return nullptr;
}

Value* Keyspace::set(std::string_view key, std::unique_ptr<Value> value, bool keep_ttl) {
  // TODO(you): replace or insert the entry (lru = now); drop the TTL unless keep_ttl.
  return nullptr;
}

bool Keyspace::erase(std::string_view key) {
  // TODO(you): expire_if_needed first: an expired key counts as missing.
  return false;
}

void Keyspace::erase_if_empty(std::string_view key) {
  // TODO(you): look at the raw entry (no lazy expiry needed); delete it and its TTL.
}

void Keyspace::clear() {
  // TODO(you): clear both dicts; dirty_ + 1; the flush hook.
}

bool Keyspace::is_expired(std::string_view key) const {
  // TODO(you): compare the TTL with the clock.
  return false;
}

bool Keyspace::set_expire(std::string_view key, int64_t when_ms) {
  // TODO(you): only for existing keys.
  return false;
}

std::optional<int64_t> Keyspace::get_expire(std::string_view key) const {
  // TODO(you): from expires_.
  return std::nullopt;
}

bool Keyspace::persist(std::string_view key) {
  // TODO(you): erase from expires_.
  return false;
}

void register_keyspace_commands(CommandTable& table, Keyspace& ks) {
  // TODO(you): port your db/strings commands to `ks` (with lookup<StringValue>), then
  // SET's expire options, TYPE and OBJECT.
}

}  // namespace rd
