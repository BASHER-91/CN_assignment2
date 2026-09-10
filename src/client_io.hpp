#ifndef CLIENT_IO_HPP
#define CLIENT_IO_HPP

#include <string>

// Send everything, waiting for writability when the socket is non-blocking.
bool send_all(int fd, const std::string& data);
bool send_line(int fd, const std::string& line);

// Multiplex stdin and the server socket. Returns 0 on a clean exit.
int run_stdio_socket_loop(int sock);

#endif
