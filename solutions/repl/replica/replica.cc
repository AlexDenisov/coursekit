#include "repl/replica/replica.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include <cstdlib>

#include "net/nonblocking/nonblocking.h"
#include "persist/rdb_reader/rdb_reader.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

bool eq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

}  // namespace

Replica::~Replica() { stop(); }

void Replica::start(std::string host, uint16_t port) {
  stop();
  host_ = std::move(host);
  port_ = port;
  active_ = true;
  connect();
}

void Replica::stop() {
  active_ = false;
  if (ack_timer_) loop_.timers().cancel(ack_timer_);
  if (retry_timer_) loop_.timers().cancel(retry_timer_);
  ack_timer_ = retry_timer_ = 0;
  if (fd_.valid()) {
    loop_.remove(fd_.get(), kReadable | kWritable);
    fd_.reset();
  }
  in_.clear();
  state_ = State::kNone;
}

void Replica::connect() {
  retry_timer_ = 0;
  if (!active_) return;
  state_ = State::kConnecting;
  fd_ = connect_nonblocking(host_, port_);
  if (!fd_.valid() || !loop_.add(fd_.get(), kWritable, [this](int, int) { on_connected(); })) {
    disconnect_and_retry();
  }
}

void Replica::on_connected() {
  loop_.remove(fd_.get(), kWritable);
  if (connect_error(fd_.get()) != 0) return disconnect_and_retry();
  int one = 1;
  ::setsockopt(fd_.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
  loop_.add(fd_.get(), kReadable, [this](int, int) { on_readable(); });
  state_ = State::kHandshake;
  handshake_step_ = 0;
  send_request({"PING"});
}

void Replica::send_request(std::initializer_list<std::string> args) {
  std::string req;
  resp::append_array_header(req, args.size());
  for (const auto& a : args) resp::append_bulk_string(req, a);
  std::string_view data = req;
  while (!data.empty() && fd_.valid()) {
    ssize_t n = ::write(fd_.get(), data.data(), data.size());
    if (n > 0) {
      data.remove_prefix(static_cast<size_t>(n));
    } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
      return;
    }
  }
}

void Replica::disconnect_and_retry() {
  if (fd_.valid()) {
    loop_.remove(fd_.get(), kReadable | kWritable);
    fd_.reset();
  }
  in_.clear();
  rdb_.clear();
  rdb_left_ = -1;
  if (ack_timer_) loop_.timers().cancel(ack_timer_);
  ack_timer_ = 0;
  state_ = active_ ? State::kConnecting : State::kNone;
  if (active_ && retry_timer_ == 0) {
    retry_timer_ = loop_.timers().add(ks_.clock().now_ms() + 100, [this](TimerId) {
      retry_timer_ = 0;
      connect();
      return kNoMore;
    });
  }
}

void Replica::on_readable() {
  IoResult io = read_available(fd_.get(), in_);
  for (;;) {
    if (state_ == State::kHandshake) {
      std::string_view v = in_.view();
      size_t e = v.find("\r\n");
      if (e == std::string_view::npos) break;
      std::string line(v.substr(0, e));
      in_.consume(e + 2);
      if (handshake_step_ == 0) {
        if (line.empty() || line[0] == '-') return disconnect_and_retry();
        send_request({"REPLCONF", "listening-port", std::to_string(listening_port_)});
        handshake_step_ = 1;
      } else if (handshake_step_ == 1) {
        send_request({"REPLCONF", "capa", "psync2"});
        handshake_step_ = 2;
      } else if (handshake_step_ == 2) {
        if (replid_.empty()) {
          send_request({"PSYNC", "?", "-1"});
        } else {
          send_request({"PSYNC", replid_, std::to_string(offset_ + 1)});
        }
        handshake_step_ = 3;
      } else {
        if (line.rfind("+FULLRESYNC ", 0) == 0) {
          size_t sp = line.find(' ', 12);
          if (sp == std::string::npos) return disconnect_and_retry();
          rdb_left_ = -1;
          rdb_.clear();
          pending_offset_ = std::strtoull(line.c_str() + sp + 1, nullptr, 10);
          replid_ = line.substr(12, sp - 12);  // adopted now; offset when the RDB is loaded
          state_ = State::kTransfer;
        } else if (line.rfind("+CONTINUE", 0) == 0) {
          if (line.size() > 10) replid_ = line.substr(10);
          ++partial_syncs_;
          state_ = State::kStreaming;
        } else {
          return disconnect_and_retry();
        }
      }
    } else if (state_ == State::kTransfer) {
      if (rdb_left_ < 0) {
        std::string_view v = in_.view();
        size_t skip = 0;
        while (skip < v.size() && v[skip] == '\n') ++skip;
        in_.consume(skip);
        v = in_.view();
        size_t e = v.find("\r\n");
        if (e == std::string_view::npos) break;
        if (v[0] != '$') return disconnect_and_retry();
        rdb_left_ = std::strtoll(std::string(v.substr(1, e - 1)).c_str(), nullptr, 10);
        in_.consume(e + 2);
      }
      size_t take = std::min<size_t>(in_.size(), static_cast<size_t>(rdb_left_));
      rdb_.append(in_.view().substr(0, take));
      in_.consume(take);
      rdb_left_ -= static_cast<int64_t>(take);
      if (rdb_left_ > 0) break;
      ks_.clear();
      rdb::LoadResult r = rdb::load_snapshot(rdb_, ks_);
      rdb_.clear();
      rdb_left_ = -1;
      if (!r.ok) return disconnect_and_retry();
      offset_ = pending_offset_;
      ++full_syncs_;
      state_ = State::kStreaming;
    } else if (state_ == State::kStreaming) {
      if (ack_timer_ == 0) {
        ack_timer_ = loop_.timers().add(ks_.clock().now_ms() + 1000, [this](TimerId) -> int64_t {
          if (state_ != State::kStreaming) {
            ack_timer_ = 0;
            return kNoMore;
          }
          send_request({"REPLCONF", "ACK", std::to_string(offset_)});
          return 1000;
        });
      }
      resp::ParseResult p = resp::parse_request(in_.view());
      if (p.status == resp::ParseStatus::kIncomplete) break;
      if (p.status == resp::ParseStatus::kError) return disconnect_and_retry();
      in_.consume(p.consumed);
      if (!p.args.empty()) {
        if (eq(p.args[0], "select")) {
          // only db 0
        } else if (eq(p.args[0], "replconf") && p.args.size() >= 2 && eq(p.args[1], "getack")) {
          send_request({"REPLCONF", "ACK", std::to_string(offset_)});
        } else {
          std::string reply;
          table_.dispatch(p.args, reply);
        }
      }
      offset_ += p.consumed;
    } else {
      break;
    }
  }
  if (io.eof || io.error) disconnect_and_retry();
}

void register_replicaof_command(CommandTable& table, Replica& replica) {
  auto fn = [&replica](CommandContext& c) {
    if (eq(c.args[1], "no") && eq(c.args[2], "one")) {
      replica.stop();
      return resp::append_simple_string(c.reply, "OK");
    }
    int64_t port;
    if (!resp::parse_int64(c.args[2], &port) || port <= 0 || port > 65535) {
      return resp::append_error(c.reply, "ERR Invalid master port");
    }
    replica.start(c.args[1], static_cast<uint16_t>(port));
    resp::append_simple_string(c.reply, "OK");
  };
  table.add({"replicaof", 3, fn});
  table.add({"slaveof", 3, fn});
}

}  // namespace rd
