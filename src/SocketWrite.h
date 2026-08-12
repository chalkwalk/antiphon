#pragma once

#include <JuceHeader.h>

#if JUCE_LINUX || JUCE_BSD || JUCE_MAC
#include <sys/socket.h>
#endif

// Writing to a socket whose peer has already closed must return an error, not
// kill the process.
//
// juce::StreamingSocket::write calls ::send with no flags (juce_Socket.cpp:532)
// and JUCE only suppresses SIGPIPE for named pipes, so on Linux the default
// disposition terminates the host -- a DAW -- the first time a player leaves a
// room mid-write. It is a narrow race, which is why it survives casual testing:
// PracticeServer provokes it reliably because it writes to several peers that
// come and go independently.
//
// The alternative, ignoring SIGPIPE process-wide, is what a standalone server
// would do, but a plugin does not get to change its host's signal disposition.
// So the suppression is per-write and platform-guarded instead.
//
// This is the only platform-specific code in src/. It is here rather than
// inlined at the call site so there is exactly one place to revisit if JUCE
// ever grows the flag itself.
namespace SocketWrite {

// Returns the number of bytes written, or -1 on error, matching
// juce::StreamingSocket::write.
inline int noSigPipe(juce::StreamingSocket &socket, const void *data,
                     int numBytes) {
  if (numBytes <= 0)
    return 0;

#if JUCE_LINUX || JUCE_BSD
  const int fd = socket.getRawSocketHandle();
  if (fd < 0)
    return -1;

  auto *p = static_cast<const char *>(data);
  int written = 0;
  while (written < numBytes) {
    const auto n = ::send(fd, p + written, (size_t)(numBytes - written),
                          MSG_NOSIGNAL);
    if (n <= 0)
      return written > 0 ? written : -1;
    written += (int)n;
  }
  return written;
#else
  // macOS carries SO_NOSIGPIPE on the socket itself (set by prepare below) and
  // Windows has no SIGPIPE at all, so the ordinary path is already safe.
  return socket.write(data, numBytes);
#endif
}

// Call once per accepted or connected socket. A no-op where the option does
// not exist.
inline void prepare(juce::StreamingSocket &socket) {
#if JUCE_MAC || JUCE_BSD
  const int fd = socket.getRawSocketHandle();
  if (fd >= 0) {
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
  }
#else
  juce::ignoreUnused(socket);
#endif
}

} // namespace SocketWrite
