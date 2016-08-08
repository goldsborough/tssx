#include <fcntl.h>
#include <sys/time.h>

#include "try-common.h"

void throw(const char *message) {
	perror(message);
	exit(EXIT_FAILURE);
}

void die(const char *message) {
	print_error(message);
	exit(EXIT_FAILURE);
}

void print_error(const char *message) {
	fputs(message, stderr);
}

void setup_timeout(struct timeval *timeout) {
	timeout->tv_sec = TIMEOUT_SECONDS;
	timeout->tv_usec = 0;
}

void set_cloexec_flag(int socket_fd) {
	// Don't leak FDs
	if (fcntl(socket_fd, F_SETFD, FD_CLOEXEC) == ERROR) {
		throw("Error using fcntl for CLOEXEC flag");
	}
}

void set_nonblocking(int socket_fd) {
	int flags;

	if ((flags = fcntl(socket_fd, F_GETFL)) == ERROR) {
		throw("Error getting socket flags to unblock\n");
	}

	flags |= O_NONBLOCK;
	if (fcntl(socket_fd, F_SETFL, flags) == ERROR) {
		throw("Error setting socket flags to unblock\n");
	}
}
