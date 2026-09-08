#include "client_io.hpp"

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
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    off += static_cast<std::size_t>(n);
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

int run_stdio_socket_loop(int sock) {
  std::string stdin_buf;
  std::string sock_buf;
  char tmp[4096];
  bool stdin_open = true;

  while (true) {
    struct pollfd fds[2];
    fds[0].fd = stdin_open ? STDIN_FILENO : -1;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = sock;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    const int rc = poll(fds, 2, -1);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::perror("poll");
      return 1;
    }

    if (stdin_open && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
      const ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
      if (n <= 0) {
        stdin_open = false;
        send_line(sock, "QUIT");
        shutdown(sock, SHUT_WR);
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
          if (!send_line(sock, line)) {
            return 1;
          }
          if (line == "QUIT") {
            stdin_open = false;
            shutdown(sock, SHUT_WR);
          }
        }
      }
    }

    if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
      const ssize_t n = recv(sock, tmp, sizeof(tmp), 0);
      if (n <= 0) {
        return 0;
      }
      sock_buf.append(tmp, static_cast<std::size_t>(n));
      std::size_t pos;
      while ((pos = sock_buf.find('\n')) != std::string::npos) {
        std::string line = sock_buf.substr(0, pos);
        sock_buf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        std::cout << line << std::endl;
      }
    }
  }
}
