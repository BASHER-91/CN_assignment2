#include "exchange.hpp"
#include "net.hpp"

#include <sys/event.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr int kEventBatch = 64;

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " [host] [port]\n"
            << "       default: 127.0.0.1 9000\n";
}

bool parse_port(const char* s, int* port) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0' || v <= 0 || v > 65535) {
    return false;
  }
  *port = static_cast<int>(v);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  ignore_sigpipe();

  std::string host = "127.0.0.1";
  int port = 9000;
  if (argc == 2) {
    if (!parse_port(argv[1], &port)) {
      usage(argv[0]);
      return 1;
    }
  } else if (argc == 3) {
    host = argv[1];
    if (!parse_port(argv[2], &port)) {
      usage(argv[0]);
      return 1;
    }
  } else if (argc != 1) {
    usage(argv[0]);
    return 1;
  }

  const int listen_fd = create_listen_socket(host, port);
  if (listen_fd < 0) {
    std::perror("listen");
    return 1;
  }

  const int kq = kqueue();
  if (kq < 0) {
    std::perror("kqueue");
    return 1;
  }

  struct kevent change;
  EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
  if (kevent(kq, &change, 1, nullptr, 0, nullptr) < 0) {
    std::perror("kevent");
    return 1;
  }

  Exchange ex(kq);
  std::cerr << "Exchange server listening on " << host << ":" << port << "\n";

  struct kevent events[kEventBatch];
  for (;;) {
    const int nready = kevent(kq, nullptr, 0, events, kEventBatch, nullptr);
    if (nready < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::perror("kevent");
      return 1;
    }

    for (int i = 0; i < nready; ++i) {
      const int fd = static_cast<int>(events[i].ident);
      if (events[i].flags & EV_ERROR) {
        if (fd != listen_fd) {
          ex.teardown(fd);
        }
        continue;
      }

      if (fd == listen_fd) {
        for (;;) {
          sockaddr_storage ss;
          socklen_t slen = sizeof(ss);
          const int cfd =
              accept(listen_fd, reinterpret_cast<sockaddr*>(&ss), &slen);
          if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ||
                errno == EMFILE || errno == ENFILE) {
              break;
            }
            std::perror("accept");
            break;
          }
          if (set_nonblock(cfd) < 0 || set_nosigpipe(cfd) < 0) {
            close_fd(cfd);
            continue;
          }
          struct kevent rev;
          EV_SET(&rev, cfd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
          if (kevent(kq, &rev, 1, nullptr, 0, nullptr) < 0) {
            close_fd(cfd);
            continue;
          }
          ex.add_client(cfd);
        }
        continue;
      }

      const bool eof = (events[i].flags & EV_EOF) != 0;
      if (events[i].filter == EVFILT_READ) {
        ex.on_read(fd, eof);
      } else if (events[i].filter == EVFILT_WRITE) {
        ex.on_write(fd);
      }
    }
  }
}
