#include "exchange.hpp"
#include "net.hpp"

#include <fcntl.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

constexpr int kEventBatch = 64;

int open_reserve_fd() { return open("/dev/null", O_RDONLY); }

void shed_pending_connections(int listen_fd, int* reserve_fd) {
  if (*reserve_fd >= 0) {
    close(*reserve_fd);
    *reserve_fd = -1;
  }
  for (;;) {
    const int cfd = accept(listen_fd, nullptr, nullptr);
    if (cfd < 0) {
      break;
    }
    close(cfd);
  }
  *reserve_fd = open_reserve_fd();
}

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " [host] [port]\n"
            << "       default: 127.0.0.1 9000\n";
}

}

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
  int reserve_fd = open_reserve_fd();
  bool fd_limit_reported = false;
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
      if (fd != listen_fd) {
        const std::uint64_t ev_session = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(events[i].udata));
        if (ex.session_of(fd) != ev_session) {
          continue;
        }
      }

      if (events[i].flags & EV_ERROR) {
        if (fd != listen_fd) {
          ex.teardown(fd);
        }
        continue;
      }

      if (fd == listen_fd) {
        for (;;) {
          const int cfd = accept(listen_fd, nullptr, nullptr);
          if (cfd < 0) {
            if (errno == EMFILE || errno == ENFILE) {
              if (!fd_limit_reported) {
                std::cerr << "accept: out of file descriptors, shedding "
                             "pending connections\n";
                fd_limit_reported = true;
              }
              shed_pending_connections(listen_fd, &reserve_fd);
              break;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
              break;
            }
            std::perror("accept");
            break;
          }
          if (set_nonblock(cfd) < 0 || set_nosigpipe(cfd) < 0) {
            close_fd(cfd);
            continue;
          }
          const std::uint64_t sid = ex.add_client(cfd);
          if (sid == 0) {
            close_fd(cfd);
            continue;
          }
          struct kevent rev;
          EV_SET(&rev, cfd, EVFILT_READ, EV_ADD, 0, 0,
                 reinterpret_cast<void*>(static_cast<std::uintptr_t>(sid)));
          if (kevent(kq, &rev, 1, nullptr, 0, nullptr) < 0) {
            ex.teardown(cfd);
            continue;
          }
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
