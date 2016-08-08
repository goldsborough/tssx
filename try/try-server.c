#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/un.h>

#include "try-common.h"
#include "try-epoll.h"
#include "try-poll.h"
#include "try-select.h"

void signal_handler(int signal_number) {
	assert(signal_number == SIGINT);
	printf("Shutting down ...\n");
	exit(EXIT_SUCCESS);
}

void connection_loop(int server_socket, Method method) {
	switch (method) {
		case SELECT: select_loop(server_socket); break;
		case POLL: poll_loop(server_socket); break;
#ifdef __APPLE__
		case EPOLL: throw("epoll not supported on OS X");
#else
		case EPOLL: epoll_loop(server_socket); break;
#endif
	}
}

int request_server_socket() {
	int server_socket;

	// Get a socket file descriptor
	if ((server_socket = socket(PF_LOCAL, SOCK_STREAM, 0)) == ERROR) {
		throw("Error getting server socket");
	}

	set_cloexec_flag(server_socket);
	set_nonblocking(server_socket);

	return server_socket;
}

void setup_server_socket(int server_socket) {
	struct sockaddr_un socket_address;
	struct sockaddr *raw_address;

	// Remove socket if it already exists
	// `remove` calls `unlink` if the path is a file,
	// else `rmdir` (so unlink is fine)
	unlink(SOCKET_PATH);

	socket_address.sun_family = AF_LOCAL;
	strcpy(socket_address.sun_path, SOCKET_PATH);

	raw_address = (struct sockaddr *)&socket_address;

	if (bind(server_socket, raw_address, SUN_LEN(&socket_address)) == ERROR) {
		throw("Error binding to socket address");
	}

	// Enable listening
	if (listen(server_socket, SOMAXCONN) == ERROR) {
		throw("Error listening on server socket");
	}
}

void install_signal_handler() {
	struct sigaction signal_action;

	signal_action.sa_handler = signal_handler;
	signal_action.sa_flags = SA_RESTART;
	sigemptyset(&signal_action.sa_mask);

	if (sigaction(SIGINT, &signal_action, NULL) == ERROR) {
		throw("Error installing signal handler");
	}
}

Method parse_method(int argc, const char *argv[]) {
	if (argc == 1) {
		return DEFAULT_METHOD;
	}

	assert(argc == 2);
	if (strcmp(argv[1], "select") == 0) {
		return SELECT;
	} else if (strcmp(argv[1], "poll") == 0) {
		return POLL;
	} else if (strcmp(argv[1], "epoll") == 0) {
		return EPOLL;
	} else {
		fprintf(stderr,
						"Invalid method '%s' not in {select, poll, epoll}",
						argv[1]);
		exit(EXIT_FAILURE);
	}
}

int main(int argc, const char *argv[]) {
	int server_socket;
	Method method;

	method = parse_method(argc, argv);

	server_socket = request_server_socket();
	setup_server_socket(server_socket);
	install_signal_handler();

	connection_loop(server_socket, method);

	close(server_socket);

	return EXIT_SUCCESS;
}
