#include "net.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

void ignore_sigpipe() { signal(SIGPIPE, SIG_IGN); }

int set_nonblock(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int set_reuseaddr(int fd) {
  int yes = 1;
  return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
}

int set_nosigpipe(int fd) {
#ifdef SO_NOSIGPIPE
  int yes = 1;
  return setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#else
  (void)fd;
  return 0;
#endif
}

static int lookup_ipv4(const std::string& host, int port, sockaddr_in* out) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICSERV;

  struct addrinfo* res = nullptr;
  const std::string port_str = std::to_string(port);
  const int rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
  if (rc != 0 || res == nullptr) {
    return -1;
  }
  std::memcpy(out, res->ai_addr, sizeof(sockaddr_in));
  freeaddrinfo(res);
  return 0;
}

int create_listen_socket(const std::string& host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  if (set_reuseaddr(fd) < 0 || set_nonblock(fd) < 0 || set_nosigpipe(fd) < 0) {
    close(fd);
    return -1;
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  if (lookup_ipv4(host, port, &addr) < 0) {
    close(fd);
    return -1;
  }

  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  // Large backlog helps accept bursts (70k bonus connection storms).
  if (listen(fd, 8192) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

int connect_to(const std::string& host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  if (set_nosigpipe(fd) < 0) {
    close(fd);
    return -1;
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  if (lookup_ipv4(host, port, &addr) < 0) {
    close(fd);
    return -1;
  }
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

void close_fd(int fd) {
  if (fd >= 0) {
    close(fd);
  }
}
