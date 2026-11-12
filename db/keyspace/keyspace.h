#pragma once

// db/keyspace: Redis's database: typed values in your dict, with TTLs and LRU clocks.
// The spec is this file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cmd/dispatch/dispatch.h"
#include "common/clock.h"
#include "ds/dict/dict.h"

namespace rd {

enum class Type { kString, kList, kHash, kSet, kZSet };

// "string", "list", "hash", "set", "zset": what TYPE replies. Given.
inline std::string_view type_name(Type t) {
  switch (t) {
    case Type::kString: return "string";
    case Type::kList: return "list";
    case Type::kHash: return "hash";
    case Type::kSet: return "set";
    case Type::kZSet: return "zset";
  }
  return "none";
}

inline constexpr char kWrongTypeError[] =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

// A value in the keyspace. Each type is a subclass with a static constexpr kType.
class Value {
 public:
  virtual ~Value() = default;
  virtual Type type() const = 0;
  // What OBJECT ENCODING replies.
  virtual std::string_view encoding() const = 0;
  // Aggregates (lists, hashes, ...) with no elements left; their key must be deleted.
  virtual bool empty() const { return false; }
  // A rough size in bytes, for db/eviction.
  virtual size_t approx_bytes() const = 0;
};

class StringValue final : public Value {
 public:
  static constexpr Type kType = Type::kString;
  explicit StringValue(std::string str = {}, bool raw_encoding = false)
      : s(std::move(str)), raw(raw_encoding) {}
  Type type() const override { return kType; }
  // Redis's rule: "raw" if `raw` is set (strings modified in place by APPEND or
  // SETRANGE); else "int" if s passes resp::parse_int64; else "embstr" if s has at most
  // 44 bytes; else "raw".
  std::string_view encoding() const override;
  size_t approx_bytes() const override { return sizeof(*this) + s.capacity(); }

  std::string s;
  bool raw = false;
};

// Redis's LRU clock: seconds, 24 bits (wraps every ~194 days). Given.
inline uint32_t lru_clock(int64_t now_ms) { return static_cast<uint32_t>((now_ms / 1000) & 0xFFFFFF); }

class Keyspace {
 public:
  struct Entry {
    std::unique_ptr<Value> value;
    uint32_t lru = 0;  // lru_clock() of the last access
  };

  explicit Keyspace(const Clock& clock) : clock_(clock) {}
  const Clock& clock() const { return clock_; }

  // ---- access (every one of these deletes the key first if its TTL has passed: lazy
  // expiry; such a key is reported missing, and its deletion doesn't count as a write) ----

  // The value at key for reading, or nullptr. Sets the entry's lru to now.
  Value* find(std::string_view key);
  // The same for a command about to modify the value in place: when found, also counts
  // as a write (dirty() + 1, the on-modified hook).
  Value* find_for_write(std::string_view key);
  // Stores value at key, replacing whatever was there; drops the key's TTL unless
  // keep_ttl. Counts as a write. Returns the stored value.
  Value* set(std::string_view key, std::unique_ptr<Value> value, bool keep_ttl = false);
  // Deletes key and its TTL. Counts as a write if it existed. False if it didn't.
  bool erase(std::string_view key);
  // Deletes key if it holds an empty aggregate (after popping the last element). Doesn't
  // count as a write by itself.
  void erase_if_empty(std::string_view key);
  // Deletes everything (FLUSHDB): every key and TTL. Counts as a write; calls the
  // on-flush hook.
  void clear();

  // Keys stored, including expired ones not yet deleted (DBSIZE).
  size_t size() const { return entries_.size(); }
  // True if key has a TTL that has passed (doesn't delete anything).
  bool is_expired(std::string_view key) const;

  // ---- TTLs: absolute Unix times in ms ----

  // Sets key's TTL. False if key doesn't exist (no lazy expiry check here).
  bool set_expire(std::string_view key, int64_t when_ms);
  std::optional<int64_t> get_expire(std::string_view key) const;
  // Removes key's TTL. True if it had one.
  bool persist(std::string_view key);

  // ---- raw access: scans, sampling, persistence ----
  Dict<Entry>& entries() { return entries_; }
  const Dict<Entry>& entries() const { return entries_; }
  Dict<int64_t>& expires() { return expires_; }
  const Dict<int64_t>& expires() const { return expires_; }

  // ---- bookkeeping ----
  // Writes so far. persist/aof and repl/replica compare it before and after a command to
  // know whether the command wrote anything.
  uint64_t dirty() const { return dirty_; }
  // Called with the key on every write to it (msg/transactions: WATCH).
  void set_on_modified(std::function<void(std::string_view)> fn) { on_modified_ = std::move(fn); }
  // Called by clear().
  void set_on_flush(std::function<void()> fn) { on_flush_ = std::move(fn); }

 private:
  // Deletes key if it has expired; true if it did.
  bool expire_if_needed(std::string_view key);
  void modified(std::string_view key);

  const Clock& clock_;
  Dict<Entry> entries_;
  Dict<int64_t> expires_;
  uint64_t dirty_ = 0;
  std::function<void(std::string_view)> on_modified_;
  std::function<void()> on_flush_;
};

// A typed lookup, for commands. Given.
//   auto [list, wrong] = lookup<ListValue>(ks, key, /*for_write=*/true);
//   if (wrong) { resp::append_error(c.reply, kWrongTypeError); return; }
//   if (!list) ...missing...
template <class T>
struct Lookup {
  T* value = nullptr;
  bool wrong_type = false;
};
template <class T>
Lookup<T> lookup(Keyspace& ks, std::string_view key, bool for_write) {
  Value* v = for_write ? ks.find_for_write(key) : ks.find(key);
  if (v == nullptr) return {};
  if (v->type() != T::kType) return {nullptr, true};
  return {static_cast<T*>(v), false};
}

// The string commands again, now on the Keyspace, with types and TTLs (arity in
// brackets). Everything db/strings did, plus:
//   - GET, INCR/DECR/INCRBY/DECRBY, APPEND, STRLEN, SET ... GET on a non-string:
//     kWrongTypeError. MGET replies null for non-strings. SET, DEL and EXISTS work on
//     any type.
//   - SET key value [NX|XX] [GET] [EX s|PX ms|EXAT unix-s|PXAT unix-ms|KEEPTTL] [-3]
//     at most one of EX/PX/EXAT/PXAT/KEEPTTL, else "ERR syntax error" (also for a
//     missing number). The number must pass parse_int64 ("ERR value is not an integer
//     or out of range") and be > 0, and seconds * 1000 must fit in int64 ("ERR invalid
//     expire time in 'set' command"). A time already in the past stores the key and
//     deletes it at once. Without KEEPTTL, SET drops any previous TTL.
//   - INCR..., APPEND keep the key's TTL; MSET drops it. APPEND marks the string raw;
//     INCR stores a fresh (non-raw) decimal string.
//   - TYPE key [2]                   +string / +list / ... / +none
//   - OBJECT ENCODING key [-2]       the value's encoding() as a bulk string; null if
//                                    missing. OBJECT ENCODING with no key:
//                                    arity_error("object|encoding"). Other subcommands:
//                                    "ERR unknown subcommand '<sub>'. Try OBJECT HELP."
//                                    (sub as sent).
void register_keyspace_commands(CommandTable& table, Keyspace& ks);

}  // namespace rd
