#include <sys/un.h>
#include <time.h>

#include "try-common.h"

void connection_loop(int client_socket) {
	char buffer[MESSAGE_SIZE];
	memset(buffer, '6', sizeof buffer);

	while (true) {
		int amount_read;

		if (write(client_socket, buffer, MESSAGE_SIZE) < MESSAGE_SIZE) {
			throw("Error writing on client side");
		}

		if ((amount_read = read(client_socket, buffer, MESSAGE_SIZE)) == 0) {
			return;
		} else if (amount_read < MESSAGE_SIZE) {
			throw("Error writing on client side");
		}

		printf(". ");
		fflush(stdout);
		sleep(1);
	}
}

int request_client_socket() {
	int client_socket;

	if ((client_socket = socket(PF_LOCAL, SOCK_STREAM, 0)) == ERROR) {
		throw("Error getting client socket");
	}

	set_cloexec_flag(client_socket);

	return client_socket;
}

void setup_client_socket(int client_socket) {
	struct sockaddr_un socket_address;
	struct sockaddr *raw_address;

	socket_address.sun_family = AF_LOCAL;
	strcpy(socket_address.sun_path, SOCKET_PATH);
	raw_address = (struct sockaddr *)&socket_address;

	if (connect(client_socket, raw_address, SUN_LEN(&socket_address)) == ERROR) {
		throw("Error connecting to socket address");
	}
}

int main(int argc, const char *argv[]) {
	int client_socket;

	client_socket = request_client_socket();
	setup_client_socket(client_socket);
	connection_loop(client_socket);

	close(client_socket);

	return EXIT_SUCCESS;
}
