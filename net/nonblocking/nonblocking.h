#pragma once

// net/nonblocking: I/O that never waits. The spec is this file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "net/buffers/buffer.h"
#include "net/sockets/sockets.h"

namespace rd {

// Puts fd in non-blocking mode (O_NONBLOCK), keeping its other flags. False on error.
bool set_nonblocking(int fd);

struct IoResult {
  size_t bytes = 0;    // bytes moved by this call
  bool eof = false;    // read only: the peer closed its side
  bool error = false;  // an error other than "would block" (EAGAIN/EWOULDBLOCK); see err
  int err = 0;         // errno when error
};

// Reads from a non-blocking fd into the back of buf, repeating until the kernel has
// nothing more for now (EAGAIN), the peer closes (eof), an error, or `max` bytes have
// been read. Retries EINTR. "Nothing available" is bytes == 0 with no eof and no error.
IoResult read_available(int fd, Buffer& buf, size_t max = std::numeric_limits<size_t>::max());

// Writes from the front of buf to a non-blocking fd until buf is empty or the kernel
// takes no more (EAGAIN), consuming what was written. Retries EINTR. Never raises
// SIGPIPE: a closed peer is an error (EPIPE).
IoResult write_available(int fd, Buffer& buf);

// Starts connecting to host:port without waiting: returns a non-blocking socket whose
// connection may still be in progress (EINPROGRESS is not an error). Invalid on error.
// The connection is done when the socket becomes writable; then ask connect_error.
Fd connect_nonblocking(std::string_view host, uint16_t port);

// After a non-blocking connect became writable: 0 if it succeeded, else its errno
// (e.g. ECONNREFUSED). getsockopt(SO_ERROR).
int connect_error(int fd);

}  // namespace rd
