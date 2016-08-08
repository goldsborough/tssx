#ifndef TRY_EPOLL_H
#define TRY_EPOLL_H

/************************ DEFINITIONS ************************/

#define MAXIMUM_NUMBER_OF_CONNECTIONS 1000

/************************ INTERFACE ************************/

void epoll_loop(int server_socket);

/************************ PRIVATE ************************/

#endif /* TRY_EPOLL_H */
