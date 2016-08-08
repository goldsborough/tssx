#include <fcntl.h>
#include <sys/time.h>

#include "try-common.h"

void throw(const char *message) {
	perror(message);
	exit(EXIT_FAILURE);
}

void terminate(const char *message) {
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
