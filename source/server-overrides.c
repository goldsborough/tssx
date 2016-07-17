#include "tssx/server-overrides.h"
#include "tssx/common-overrides.h"
#include "tssx/poll-overrides.h"

#include <sys/socket.h>
#include <sys/types.h>

/******************** OVERRIDES ********************/

int accept(int server_socket, sockaddr *address, socklen_t *length) {
	int client_socket;
	int use_tssx;

	if ((client_socket = real_accept(server_socket, address, length)) == ERROR) {
		return ERROR;
	}

	if ((use_tssx = server_check_use_tssx(server_socket)) == ERROR) {
		return ERROR;
	} else if (!use_tssx) {
		return client_socket;
	}

	return setup_tssx(client_socket);
}

ssize_t read(int key, void *destination, size_t requested_bytes) {
	// clang-format off
	return connection_read(
		key,
		destination,
		requested_bytes,
		CLIENT_BUFFER
	);
	// clang-format on
}

ssize_t write(int key, const void *source, size_t requested_bytes) {
	// clang-format off
	return connection_write(
		key,
		source,
		requested_bytes,
		SERVER_BUFFER
	);
	// clang-format on
}

/******************** HELPERS ********************/

int send_segment_id_to_client(int client_socket, Session *session) {
	int return_code;

	// clang-format off
	return_code = real_write(
		client_socket,
		&session->connection->segment_id,
		sizeof session->connection->segment_id
	);
	// clang-format on

	if (return_code == ERROR) {
		print_error("Error sending segment ID to client");
		disconnect(session->connection);
		return ERROR;
	}

	return return_code;
}

int setup_tssx(int client_socket) {
	Session session;
	ConnectionOptions options;
	key_t key;

	session.socket = client_socket;
	options = options_from_socket(client_socket, SERVER);
	session.connection = create_connection(&options);

	if (session.connection == NULL) {
		print_error("Error allocating connection resources");
		return ERROR;
	}

	if (send_segment_id_to_client(client_socket, &session) == ERROR) {
		return ERROR;
	}

	// Returns the key generated for this connection
	key = bridge_generate_key(&bridge);
	bridge_insert(&bridge, key, &session);

	return key;
}

/******************** "POLYMORPHIC" FUNCTIONS ********************/

void set_non_blocking(Connection *connection, bool non_blocking) {
	connection->server_buffer->timeouts.non_blocking[WRITE] = non_blocking;
	connection->client_buffer->timeouts.non_blocking[READ] = non_blocking;
}

bool get_non_blocking(Connection *connection) {
	assert(connection->server_buffer->timeouts.non_blocking[WRITE] ==
				 connection->client_buffer->timeouts.non_blocking[READ]);
	return connection->server_buffer->timeouts.non_blocking[WRITE];
}

bool ready_for(Connection *connection, Operation operation) {
	if (operation == READ) {
		return !buffer_is_empty(connection->client_buffer);
	} else {
		return !buffer_is_full(connection->server_buffer);
	}
}
