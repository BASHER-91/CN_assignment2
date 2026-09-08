#ifndef NET_HPP
#define NET_HPP

#include <string>

void ignore_sigpipe();

int set_nonblock(int fd);
int set_reuseaddr(int fd);
int set_nosigpipe(int fd);

// Bind and listen on IPv4 host:port. Returns listen fd or -1.
int create_listen_socket(const std::string& host, int port);

// Blocking TCP connect. Returns fd or -1.
int connect_to(const std::string& host, int port);

void close_fd(int fd);

#endif
