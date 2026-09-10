#ifndef NET_HPP
#define NET_HPP

#include <string>

void ignore_sigpipe();
int set_nonblock(int fd);
int set_nosigpipe(int fd);
bool parse_port(const char* text, int* port);
int create_listen_socket(const std::string& host, int port);
int connect_to(const std::string& host, int port);
void close_fd(int fd);

#endif
