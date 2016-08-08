#ifndef TRY_SELECT_H
#define TRY_SELECT_H

#include <sys/select.h>

/************************ DEFINITIONS ************************/

/************************ INTERFACE ************************/

void select_loop(int server_socket);

/************************ PRIVATE ************************/

void _accept_connections(const fd_set* read_set,
												 int server_socket,
												 fd_set* sockets,
												 int* highest_fd);

void _handle_select_requests(const fd_set* read_set,
														 fd_set* sockets,
														 int highest_fd,
														 int server_socket);

#endif /* TRY_SELECT_H */
