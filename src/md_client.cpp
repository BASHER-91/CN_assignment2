#include "client_io.hpp"
#include "net.hpp"
#include "protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void usage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " <host> <port> [instrument]\n";
}

static bool parse_port(const char* s, int* port) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0' || v <= 0 || v > 65535) {
    return false;
  }
  *port = static_cast<int>(v);
  return true;
}

int main(int argc, char** argv) {
  ignore_sigpipe();
  if (argc != 3 && argc != 4) {
    usage(argv[0]);
    return 1;
  }

  int port = 0;
  if (!parse_port(argv[2], &port)) {
    usage(argv[0]);
    return 1;
  }

  if (argc == 4 && parse_instrument(argv[3]) == kInstInvalid) {
    std::cerr << "unknown instrument (use JNST or IMCT)\n";
    return 1;
  }

  const int sock = connect_to(argv[1], port);
  if (sock < 0) {
    std::perror("connect");
    return 1;
  }

  if (argc == 4) {
    if (!send_line(sock, std::string("SUBSCRIBE ") + argv[3])) {
      std::cerr << "failed to send SUBSCRIBE\n";
      close_fd(sock);
      return 1;
    }
  }

  const int rc = run_stdio_socket_loop(sock);
  close_fd(sock);
  return rc;
}
