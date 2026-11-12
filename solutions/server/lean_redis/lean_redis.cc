#include "server/lean_redis/lean_redis.h"

#include <malloc/malloc.h>

#include <string>

#include "db/zsets/zsets.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

size_t heap_bytes_in_use() {
  malloc_statistics_t stats;
  malloc_zone_statistics(nullptr, &stats);
  return stats.size_in_use;
}

LeanRedis::LeanRedis(const Clock& clock, LeanRedisOptions options)
    : clock_(clock),
      base_(clock, options.base),
      rng_(options.base.seed + 1),
      evictor_(base_.keyspace(), rng_, options.eviction, [] { return heap_bytes_in_use(); }) {
  register_config_command();
  register_zset_commands(base_.table(), base_.keyspace());
  base_.server().set_new_client_hook([this](Client& client) {
    client.set_runner([this](Client& c, std::span<const std::string> args, std::string& reply) {
      if (is_denyoom(args[0])) {
        int64_t now = clock_.now_ms();
        if (now != last_memory_check_ms_ || !memory_ok_) {
          last_memory_check_ms_ = now;
          memory_ok_ = evictor_.make_room();
        }
        if (!memory_ok_) {
          resp::append_error(reply, kOomError);
          return;
        }
      }
      base_.table().dispatch(args, reply, &c);
    });
  });
}

// Given: CONFIG GET <maxmemory|maxmemory-policy>, CONFIG SET <maxmemory|maxmemory-policy>
// <value>; anything else is refused.
void LeanRedis::register_config_command() {
  base_.table().add({"config", -3, [this](CommandContext& c) {
                       std::string sub = c.args[1], param = c.args[2];
                       for (auto* s : {&sub, &param}) {
                         for (char& ch : *s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                       }
                       EvictionConfig cfg = evictor_.config();
                       static const char* const kPolicyNames[] = {"noeviction", "allkeys-lru", "volatile-lru",
                                                                  "allkeys-random", "volatile-random", "volatile-ttl"};
                       if (sub == "get" && c.args.size() == 3) {
                         if (param == "maxmemory") {
                           resp::append_array_header(c.reply, 2);
                           resp::append_bulk_string(c.reply, "maxmemory");
                           return resp::append_bulk_string(c.reply, std::to_string(cfg.maxmemory));
                         }
                         if (param == "maxmemory-policy") {
                           resp::append_array_header(c.reply, 2);
                           resp::append_bulk_string(c.reply, "maxmemory-policy");
                           return resp::append_bulk_string(c.reply, kPolicyNames[static_cast<int>(cfg.policy)]);
                         }
                         return resp::append_array_header(c.reply, 0);
                       }
                       if (sub == "set" && c.args.size() == 4) {
                         if (param == "maxmemory") {
                           int64_t v;
                           if (!resp::parse_int64(c.args[3], &v) || v < 0) {
                             return resp::append_error(c.reply, "ERR CONFIG SET failed: invalid maxmemory");
                           }
                           cfg.maxmemory = static_cast<size_t>(v);
                         } else if (param == "maxmemory-policy") {
                           auto p = parse_policy(c.args[3]);
                           if (!p) return resp::append_error(c.reply, "ERR CONFIG SET failed: invalid maxmemory-policy");
                           cfg.policy = *p;
                         } else {
                           return resp::append_error(c.reply, "ERR Unknown option or number of arguments for CONFIG SET");
                         }
                         evictor_.set_config(cfg);
                         last_memory_check_ms_ = -1;  // re-check on the next write
                         return resp::append_simple_string(c.reply, "OK");
                       }
                       resp::append_error(c.reply, "ERR unsupported CONFIG subcommand");
                     }});
}

}  // namespace rd
