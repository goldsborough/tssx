#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <sys/un.h>

#include "tssx/client-overrides.h"
#include "tssx/common-overrides.h"
#include "utility/sockets.h"

int connect(int fd, const sockaddr* address, socklen_t length) {
	int use_tssx;

	if (real_connect(fd, address, length) == ERROR) {
		return ERROR;
	}

	if ((use_tssx = check_tssx_usage(fd, CLIENT)) == ERROR) {
		print_error("Could not check if socket uses TSSX");
		return ERROR;
	} else if (!use_tssx) {
		return SUCCESS;
	}

	return _setup_tssx(fd);
}

ssize_t read(int fd, void* destination, size_t requested_bytes) {
	// clang-format off
	return connection_read(
		fd,
		destination,
		requested_bytes,
		SERVER_BUFFER
	);
	// clang-format on
}

ssize_t write(int fd, const void* source, size_t requested_bytes) {
	// clang-format off
	return connection_write(
		fd,
		source,
		requested_bytes,
		CLIENT_BUFFER
	);
	// clang-format on
}

/******************** HELPERS ********************/

int _setup_tssx(int fd) {
	int segment_id;
	Session session;
	ConnectionOptions options;

	// Read the options first
	options = options_from_socket(fd, CLIENT);

	segment_id = _read_segment_id_from_server(fd);
	if (segment_id == ERROR) {
		return ERROR;
	}

	session.connection = setup_connection(segment_id, &options);
	if (session.connection == NULL) {
		return ERROR;
	}

	if (_synchronize_with_server(fd) == ERROR) {
		print_error("Error synchronizing with server during setup\n");
		return ERROR;
	}

	return bridge_insert(&bridge, fd, &session);
}

int _read_segment_id_from_server(int client_socket) {
	int return_code;
	int segment_id;
	int flags;

	// Get the old flags and unset the non-blocking flag (if set)
	flags = unset_socket_non_blocking(client_socket);

	// clang-format off
	return_code = real_read(
		client_socket,
		&segment_id,
		sizeof segment_id
	);
	// clang-format on

	// Put the old flags back in place
	// set_socket_flags(client_socket, flags);

	// We do need to keep the socket open though, so that
	// its descriptor cannot be reused by the operating system

	if (return_code == ERROR) {
		print_error("Error receiving segment ID on client side");
		return ERROR;
	}

	return segment_id;
}

int _synchronize_with_server(int fd) {
	return (real_write(fd, "!", 1) == ERROR) ? ERROR : SUCCESS;
}

/******************** "POLYMORPHIC" FUNCTIONS ********************/

void set_non_blocking(Connection* connection, bool non_blocking) {
	connection->server_buffer->timeouts.non_blocking[READ] = non_blocking;
	connection->client_buffer->timeouts.non_blocking[WRITE] = non_blocking;
}

bool is_non_blocking(Connection* connection) {
	assert(connection->server_buffer->timeouts.non_blocking[READ] ==
				 connection->client_buffer->timeouts.non_blocking[WRITE]);
	return connection->client_buffer->timeouts.non_blocking[WRITE];
}

bool _ready_for(Connection* connection, Operation operation) {
	assert(connection != NULL);
	if (operation == READ) {
		return buffer_ready_for(connection->server_buffer, READ);
	} else {
		return buffer_ready_for(connection->client_buffer, WRITE);
	}
}
