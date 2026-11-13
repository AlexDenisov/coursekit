#include "msg/transactions/transactions.h"

#include "resp/encoder/encoder.h"

namespace rd {

Transactions::Transactions(Keyspace& ks) : ks_(ks) {
  // TODO(you): ks.set_on_modified(...) and ks.set_on_flush(...).
}

bool Transactions::handle(const CommandTable& table, Session& s, std::span<const std::string> args, std::string& reply) {
  // TODO(you): the table in transactions.h.
  return false;
}

bool Transactions::in_multi(const Session& s) const {
  // TODO(you)
  return false;
}

void Transactions::unwatch_all(Session& s) {
  // TODO(you): remove s from watchers_ for each of its keys (dropping empty entries).
}

void Transactions::forget(Session& s) {
  // TODO(you)
}

}  // namespace rd
