#include <fcntl.h>

#include "try-common.h"
#include "try-select.h"

/************************ INTERFACE ************************/

void select_loop(int server_socket) {
	int highest_fd;
	fd_set sockets;
	struct timeval timeout;

	setup_timeout(&timeout);
	FD_ZERO(&sockets);
	FD_SET(server_socket, &sockets);
	highest_fd = server_socket;

	while (true) {
		fd_set read_set;
		FD_COPY(&sockets, &read_set);

		switch (select(highest_fd + 1, &read_set, NULL, NULL, &timeout)) {
			case ERROR: throw("Error on select"); break;
			case TIMEOUT: terminate("Timeout on select\n"); break;
		}

		_accept_connections(&read_set, server_socket, &sockets, &highest_fd);
		_handle_select_requests(&read_set, &sockets, highest_fd, server_socket);
	}
}

/************************ PRIVATE ************************/

void _accept_connections(const fd_set* read_set,
												 int server_socket,
												 fd_set* sockets,
												 int* highest_fd) {
	int client_socket;

	if (!FD_ISSET(server_socket, read_set)) return;

	if ((client_socket = accept(server_socket, NULL, NULL)) == ERROR) {
		throw("Error accepting connection on server side");
	}

	if (fcntl(client_socket, F_SETFL, O_NONBLOCK) == ERROR) {
		throw("Error setting client socket non-blocking\n");
	}

	if (client_socket > *highest_fd) {
		*highest_fd = client_socket;
	}

	// Add to master set
	FD_SET(client_socket, sockets);

	printf("New connection %d\n", client_socket);
}

void _handle_select_requests(const fd_set* read_set,
														 fd_set* sockets,
														 int highest_fd,
														 int server_socket) {
	int code;
	char buffer[MESSAGE_SIZE];
	memset(buffer, '6', sizeof buffer);

	for (int fd = 3; fd <= highest_fd; ++fd) {
		if (fd == server_socket) continue;
		if (!FD_ISSET(fd, read_set)) continue;

		// read()/recv() returns zero when the peer disconnects
		if ((code = read(fd, buffer, MESSAGE_SIZE)) == 0) {
			FD_CLR(fd, sockets);
		} else if (code < MESSAGE_SIZE) {
			// throw("Error reading on server side");
		} else {
			if (write(fd, buffer, MESSAGE_SIZE) < MESSAGE_SIZE) {
				throw("Error writing on server side");
			}
		}
	}
}
