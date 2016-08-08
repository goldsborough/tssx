#ifndef TRY_EPOLL_H
#define TRY_EPOLL_H

#include <stddef.h>

/************************ DEFINITIONS ************************/

#define MAXIMUM_NUMBER_OF_CONNECTIONS 1000

struct epoll_event;

/************************ INTERFACE ************************/

void epoll_loop(int server_socket);

/************************ PRIVATE ************************/

int _request_epoll_fd();

void _register_epoll_socket(int epfd, int socket_fd);

void _remove_epoll_socket(int epfd, int socket_fd);

void _accept_epoll_connections(int epfd, int server_socket);

void _handle_epoll_requests(int epfd,
														int server_socket,
														struct epoll_event* events,
			    size_t number_of_events);

#endif /* TRY_EPOLL_H */
