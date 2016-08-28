#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/time.h>
#include <time.h>

#define __USE_GNU
#include <pthread.h>
#include <sched.h>

#include "utility/utility.h"

void throw(const char* message) {
	perror(message);
	exit(EXIT_FAILURE);
}

void terminate(const char* message) {
	fputs(message, stderr);
	exit(EXIT_FAILURE);
}

void print_error(const char* message) {
	fprintf(stderr, "%s\n", message);
}

void warn(const char* message) {
	fprintf(stderr, "\033[33mWarning\033[0m: %s\n", message);
}

int generate_key(const char* path) {
	// Generate a random key from the given file path
	// (inode etc.) plus the arbitrary character
	return ftok(path, 'X');
}

void nsleep(int nanoseconds) {
	struct timespec time = {0, nanoseconds};
	if (nanosleep(&time, NULL) == -1) {
		throw("Sleep was interrupted");
	}
}

int current_milliseconds() {
	struct timeval current_time;

	if (gettimeofday(&current_time, NULL) == -1) {
		throw("Error getting time");
	}

	return timeval_to_milliseconds(&current_time);
}

int timeval_to_milliseconds(const struct timeval* time) {
	int milliseconds;

	assert(time != NULL);

	milliseconds = time->tv_sec * 1000;
	milliseconds += time->tv_usec / 1000;

	return milliseconds;
}

void timespec_to_timeval(const struct timespec* source,
												 struct timeval* destination) {
	assert(destination != NULL);

	if (source == NULL) return;

	destination->tv_sec = source->tv_sec;
	destination->tv_usec = source->tv_nsec / 1000;
}

void pin_thread(int where) {
#ifdef __linux__
	// Doesn't work on OS X right now
	int j;
	cpu_set_t cpuset;
	pthread_t thread;
	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(where, &cpuset);
	pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	int s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		fprintf(stderr, "error: pthread_getaffinity_np");
		exit(-1);
	}
#endif
}
