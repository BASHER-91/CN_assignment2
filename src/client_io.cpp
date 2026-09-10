#include "client_io.hpp"

#include "net.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <string>

bool send_all(int fd, const std::string& data) {
  std::size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = send(fd, data.data() + off, data.size() - off, 0);
    if (n > 0) {
      off += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Wait for writability so this helper is also correct on a non-blocking
      // socket instead of reporting a spurious failure.
      struct pollfd p;
      p.fd = fd;
      p.events = POLLOUT;
      p.revents = 0;
      if (poll(&p, 1, -1) < 0 && errno != EINTR) {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

bool send_line(int fd, const std::string& line) {
  std::string s = line;
  if (s.empty() || s.back() != '\n') {
    s.push_back('\n');
  }
  return send_all(fd, s);
}

namespace {

void queue_line(std::string* out_buf, const std::string& line) {
  *out_buf += line;
  if (line.empty() || line.back() != '\n') {
    out_buf->push_back('\n');
  }
}

bool flush_nonblocking(int sock, std::string* out_buf) {
  while (!out_buf->empty()) {
    const ssize_t n = send(sock, out_buf->data(), out_buf->size(), 0);
    if (n > 0) {
      out_buf->erase(0, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }
    return false;
  }
  return true;
}

void print_complete_lines(std::string* sock_buf) {
  std::size_t pos;
  while ((pos = sock_buf->find('\n')) != std::string::npos) {
    std::string line = sock_buf->substr(0, pos);
    sock_buf->erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::cout << line << std::endl;
  }
}

}  // namespace

int run_stdio_socket_loop(int sock) {
  if (set_nonblock(sock) < 0) {
    std::perror("fcntl");
    return 1;
  }

  std::string stdin_buf;
  std::string sock_buf;
  std::string out_buf;
  char tmp[4096];
  bool stdin_open = true;
  bool shutdown_pending = false;
  bool write_shutdown = false;

  while (true) {
    struct pollfd fds[2];
    fds[0].fd = stdin_open ? STDIN_FILENO : -1;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = sock;
    fds[1].events = POLLIN | (out_buf.empty() ? 0 : POLLOUT);
    fds[1].revents = 0;

    const int rc = poll(fds, 2, -1);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::perror("poll");
      return 1;
    }

    if (fds[1].revents & POLLNVAL) {
      return 1;
    }

    // Read server messages before processing more stdin so asynchronous
    // notifications are not delayed by client output.
    if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
      const ssize_t n = recv(sock, tmp, sizeof(tmp), 0);
      if (n > 0) {
        sock_buf.append(tmp, static_cast<std::size_t>(n));
        print_complete_lines(&sock_buf);
      } else if (n == 0) {
        return 0;
      } else if (errno != EINTR && errno != EAGAIN &&
                 errno != EWOULDBLOCK) {
        return 1;
      } else if ((fds[1].revents & POLLHUP) &&
                 (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
      }
    }

    bool output_changed = false;
    if (stdin_open && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
      const ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
      if (n == 0) {
        stdin_open = false;
        shutdown_pending = true;
        queue_line(&out_buf, "QUIT");
        output_changed = true;
      } else if (n < 0) {
        if (errno != EINTR) {
          return 1;
        }
      } else {
        stdin_buf.append(tmp, static_cast<std::size_t>(n));
        std::size_t pos;
        while ((pos = stdin_buf.find('\n')) != std::string::npos) {
          std::string line = stdin_buf.substr(0, pos);
          stdin_buf.erase(0, pos + 1);
          if (!line.empty() && line.back() == '\r') {
            line.pop_back();
          }
          if (line.empty()) {
            continue;
          }
          queue_line(&out_buf, line);
          output_changed = true;
          if (line == "QUIT") {
            stdin_open = false;
            shutdown_pending = true;
            stdin_buf.clear();
            break;
          }
        }
      }
    }

    if (!out_buf.empty() &&
        (output_changed || (fds[1].revents & POLLOUT))) {
      if (!flush_nonblocking(sock, &out_buf)) {
        return 1;
      }
    }

    if (shutdown_pending && out_buf.empty() && !write_shutdown) {
      if (shutdown(sock, SHUT_WR) < 0 && errno != ENOTCONN) {
        return 1;
      }
      write_shutdown = true;
    }
  }
}
