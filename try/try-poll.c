#include <fcntl.h>
#include <poll.h>

#include "try-common.h"
#include "try-poll.h"

/************************ INTERFACE ************************/

void poll_loop(int server_socket) {
	struct pollfd poll_entries[MAXIMUM_NUMBER_OF_CONNECTIONS];
	size_t number_of_connections;

	_setup_server_polling(poll_entries, server_socket);
	// The listener socket only (right now)
	number_of_connections = 1;

	while (true) {
	  		puts("6\n");
		switch (poll(poll_entries, number_of_connections, TIMEOUT)) {
			case ERROR: throw("Error on poll"); break;
			case 0: die("Timeout on poll\n"); break;
		}
		puts("3\n");
		_accept_poll_connections(poll_entries,
														 server_socket,
														 &number_of_connections);
				puts("4\n");
		_handle_poll_requests(poll_entries, &number_of_connections);
				puts("5\n");
	}
}

void _setup_server_polling(struct pollfd* poll_entries, int server_socket) {
	poll_entries[0].fd = server_socket;
	poll_entries[0].events = POLLIN;
}

void _accept_poll_connections(struct pollfd* poll_entries,
															int server_socket,
															size_t* number_of_connections) {
	int client_socket;
	if (!(poll_entries[0].revents & POLLIN)) return;

	if ((client_socket = accept(server_socket, NULL, NULL)) == ERROR) {
		throw("Error accepting connection on server side");
	}

	set_nonblocking(client_socket);

	_setup_client_polling(poll_entries, client_socket, *number_of_connections);

	assert(*number_of_connections < MAXIMUM_NUMBER_OF_CONNECTIONS);
	++(*number_of_connections);

	printf("New connection: %d\n", client_socket);
}

void _setup_client_polling(struct pollfd* poll_entries,
													 int client_socket,
													 int number_of_connections) {
	poll_entries[number_of_connections].fd = client_socket;
	// Listen for read and hangup events (connection lost)
	poll_entries[number_of_connections].events = POLLIN | POLLHUP;
	poll_entries[number_of_connections].revents = 0;
}

void _handle_poll_requests(struct pollfd* poll_entries,
													 size_t* number_of_connections) {
	char buffer[MESSAGE_SIZE];
	int amount_read;

	// The first entry is the listener socket
	for (size_t index = 1; index < *number_of_connections; /* in loop */) {
	  
		if (poll_entries[index].revents & POLLHUP) {
			_handle_poll_disconnect(poll_entries, index, number_of_connections);
			continue;
		}

		puts("1\n");

		if (!(poll_entries[index].revents & POLLIN)) {
			++index;
			continue;
		}

				puts("2\n");
		amount_read = read(poll_entries[index].fd, buffer, MESSAGE_SIZE);
				puts("3\n");

		if (amount_read == 0) {
			_handle_poll_disconnect(poll_entries, index, number_of_connections);
			continue;
		} else if (amount_read < MESSAGE_SIZE) {
			throw("Error reading on server side");
		}

		if (write(poll_entries[index].fd, buffer, MESSAGE_SIZE) < MESSAGE_SIZE) {
			throw("Error writing on server side");
		}
		++index;
	}
}

void _handle_poll_disconnect(struct pollfd* poll_entries,
														 size_t index,
														 size_t* number_of_connections) {
	assert(index < *number_of_connections);

	printf("Death to client %d\n", poll_entries[index].fd);

	close(poll_entries[index].fd);

	// Shift entries to the left
	for (++index; index < *number_of_connections; ++index) {
		poll_entries[index - 1] = poll_entries[index];
	}

	--(*number_of_connections);
	poll_entries[*number_of_connections].fd = -1;
}
