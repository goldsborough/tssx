#include <sys/epoll.h>

#include "try-common.h"
#include "try-epoll.h"

/************************ INTERFACE ************************/

void epoll_loop(int server_socket) {
	struct epoll_event events[MAXIMUM_NUMBER_OF_CONNECTIONS];
	int epfd;
	int timeout;

	timeout = TIMEOUT * 1000;
	epfd = _request_epoll_fd();
	_register_epoll_socket(epfd, server_socket);

	while (true) {
		int number_of_events;

		number_of_events = epoll_wait(epfd, events, sizeof events, timeout);
		switch (number_of_events) {
			case ERROR: throw("Error on epoll");
			case 0: die("Timeout on epoll");
		}
		_handle_epoll_requests(epfd, server_socket, events, number_of_events);
	}

	close(epfd);
}

int _request_epoll_fd() {
	int epfd;

	if ((epfd = epoll_create1(EPOLL_CLOEXEC)) == ERROR) {
		throw("Error requesting epoll master file descriptor");
	}

	return epfd;
}

void _register_epoll_socket(int epfd, int socket_fd) {
	struct epoll_event event;

	event.events = EPOLLIN;
	event.data.fd = socket_fd;

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd, &event) == ERROR) {
		throw("Error adding socket to epoll instance");
	}
}

void _remove_epoll_socket(int epfd, int socket_fd) {
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, socket_fd, NULL) == ERROR) {
		throw("Error removing socket from epoll instance");
	}

	if (close(socket_fd) == ERROR) {
		throw("Error closing socket descriptor");
	}

	printf("Death to client %d\n", socket_fd);
}

void _accept_epoll_connections(int epfd, int server_socket) {
	int client_socket;

	if ((client_socket = accept(server_socket, NULL, NULL)) == ERROR) {
		throw("Error accepting connection on server side");
	}

	_register_epoll_socket(epfd, client_socket);

	printf("New connection: %d\n", client_socket);
}

void _handle_epoll_requests(int epfd,
														int server_socket,
														struct epoll_event* events,
														size_t number_of_events) {
	char buffer[MESSAGE_SIZE];
	assert(number_of_events > 0);

	for (size_t index = 0; index < number_of_events; ++index) {
		int fd = events[index].data.fd;

		assert(events[index].events & EPOLLIN);

		if (fd == server_socket) {
			_accept_epoll_connections(epfd, server_socket);
		} else {
			int amount_read;

			if ((amount_read = read(fd, buffer, MESSAGE_SIZE)) == 0) {
				_remove_epoll_socket(epfd, fd);
			} else if (amount_read < MESSAGE_SIZE) {
				throw("Error reading on server side");
			} else {
				if (write(fd, buffer, MESSAGE_SIZE) < MESSAGE_SIZE) {
					throw("Error writing on server side");
				}
			}
		}
	}
}
