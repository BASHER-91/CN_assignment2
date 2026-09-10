#ifndef CLIENT_IO_HPP
#define CLIENT_IO_HPP

#include <string>

bool send_line(int fd, const std::string& line);
int run_stdio_socket_loop(int sock);

#endif
