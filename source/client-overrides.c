#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "common/sockets.h"
#include "tssx/client-overrides.h"
#include "tssx/common-overrides.h"
#include "tssx/poll-overrides.h"

int socket(int domain, int type, int protocol) {
	int socket_fd = real_socket(domain, type, protocol);

	if (socket_is_stream_and_domain(domain, type)) {
		// Note: this is no matter if we select the socket to use TSSX or not!
		Session session = {socket_fd, NULL};
		key_t key = bridge_generate_key(&bridge);
		bridge_insert(&bridge, key, &session);
		return key;
	} else {
		// For internet sockets, UDP sockets etc.
		return socket_fd;
	}
}

int connect(int key, const sockaddr* address, socklen_t length) {
	Session* session;
	if (key < TSSX_KEY_OFFSET) {
		// In this case the key is actually the socket FD
		return real_connect(key, address, length);
	}

	// Lookup the session and stored socket FD
	session = bridge_lookup(&bridge, key);
	if (real_connect(session->socket, address, length) == -1) {
		return ERROR;
	}

	return setup_tssx(session, address);
}

ssize_t read(int key, void* destination, size_t requested_bytes) {
	// clang-format off
	return connection_read(
		key,
		destination,
		requested_bytes,
		SERVER_BUFFER
	);
	// clang-format on
}

ssize_t write(int key, const void* source, size_t requested_bytes) {
	// clang-format off
	return connection_write(
		key,
		source,
		requested_bytes,
		CLIENT_BUFFER
	);
	// clang-format on
}

/******************** HELPERS ********************/

int read_segment_id_from_server(int client_socket) {
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
	// Does it make sense to put non-blocking back in place? (else comment)
	set_socket_flags(client_socket, flags);

	if (return_code == ERROR) {
		print_error("Error receiving segment ID on client side");
		return ERROR;
	}

	return segment_id;
}

int setup_tssx(Session* session, const sockaddr* address) {
	int segment_id;
	int use_tssx;
	ConnectionOptions options;

	if ((use_tssx = client_check_use_tssx(session->socket, address)) == ERROR) {
		print_error("Could not check if socket uses TSSX");
		return ERROR;
	} else if (!use_tssx) {
		assert(session->connection == NULL);
		session->connection = NULL;
		return SUCCESS;
	}

	// Read the options first
	options = options_from_socket(session->socket, CLIENT);

	segment_id = read_segment_id_from_server(session->socket);
	if (segment_id == ERROR) {
		return ERROR;
	}

	session->connection = setup_connection(segment_id, &options);
	if (session->connection == NULL) {
		return ERROR;
	}

	return SUCCESS;
}

/******************** "POLYMORPHIC" FUNCTIONS ********************/

void set_non_blocking(Connection* connection, bool non_blocking) {
	connection->server_buffer->timeouts.non_blocking[READ] = non_blocking;
	connection->client_buffer->timeouts.non_blocking[WRITE] = non_blocking;
}

bool get_non_blocking(Connection* connection) {
	assert(connection->server_buffer->timeouts.non_blocking[READ] ==
				 connection->client_buffer->timeouts.non_blocking[WRITE]);
	return connection->client_buffer->timeouts.non_blocking[WRITE];
}

bool ready_for(Connection* connection, Operation operation) {
	if (operation == READ) {
		return !buffer_is_empty(connection->server_buffer);
	} else {
		return !buffer_is_full(connection->client_buffer);
	}
}
