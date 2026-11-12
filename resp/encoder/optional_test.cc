#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "resp/encoder/encoder.h"

namespace rd::resp {
namespace {

std::string command(std::vector<std::string_view> args) {
  std::string out;
  append_command(out, args);
  return out;
}

// The example from the RESP specification (redis.io/docs/latest/develop/reference/protocol-spec).
TEST(AppendCommand, SpecExample) {
  EXPECT_EQ(command({"LLEN", "mylist"}), "*2\r\n$4\r\nLLEN\r\n$6\r\nmylist\r\n");
}

TEST(AppendCommand, ArgumentsAreBinarySafe) {
  EXPECT_EQ(command({"SET", "k", "a\r\nb"}), "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$4\r\na\r\nb\r\n");
}

TEST(AppendCommand, EmptyArgument) {
  EXPECT_EQ(command({"ECHO", ""}), "*2\r\n$4\r\nECHO\r\n$0\r\n\r\n");
}

}  // namespace
}  // namespace rd::resp
