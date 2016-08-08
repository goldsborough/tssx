#ifndef TRY_POLL_H
#define TRY_POLL_H

#include <stddef.h>

/************************ DEFINITIONS ************************/

#define MAXIMUM_NUMBER_OF_CONNECTIONS 1000

struct pollfd;

/************************ INTERFACE ************************/

void poll_loop(int server_socket);

/************************ PRIVATE ************************/

void _setup_server_polling(struct pollfd* poll_entries, int server_socket);
void _setup_client_polling(struct pollfd* poll_entries,
													 int client_socket,
													 int number_of_connections);

void _accept_poll_connections(struct pollfd* poll_entries,
															int server_socket,
															size_t* number_of_connections);
void _handle_poll_requests(struct pollfd* poll_entries,
													 size_t* number_of_connections);

void _handle_poll_disconnect(struct pollfd* poll_entries,
														 size_t index,
														 size_t* number_of_connections);

#endif /* TRY_POLL_H */
