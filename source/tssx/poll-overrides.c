#define _GNU_SOURCE

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>

#include "tssx/bridge.h"
#include "tssx/poll-overrides.h"
#include "tssx/session.h"
#include "tssx/vector.h"
#include "utility/common.h"

/******************** REAL FUNCTIONS ********************/

int real_poll(struct pollfd fds[], nfds_t nfds, int timeout) {
	return ((real_poll_t)dlsym(RTLD_NEXT, "poll"))(fds, nfds, timeout);
}

/******************** OVERRIDES ********************/

int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
	Vector tssx_fds, other_fds;
	int ready_count;

	if (nfds == 0) return 0;

	if (_partition(&tssx_fds, &other_fds, fds, nfds) == ERROR) {
		return ERROR;
	}

	if (tssx_fds.size == 0) {
		// We are only dealing with normal (non-tssx) fds
		ready_count = real_poll(fds, nfds, timeout);
	} else if (other_fds.size == 0) {
		// We are only dealing with tssx connections
		ready_count = _simple_tssx_poll(&tssx_fds, timeout);
	} else {
		ready_count = _concurrent_poll(&tssx_fds, &other_fds, timeout);
	}

	_cleanup(&tssx_fds, &other_fds);

	return ready_count;
}

/******************** HELPERS ********************/

const short _operation_map[2] = {POLLIN, POLLOUT};

int _partition(Vector* tssx_fds,
							 Vector* other_fds,
							 struct pollfd fds[],
							 nfds_t number_of_fds) {
	// Minimum capacity of 16 each
	if (vector_setup(tssx_fds, 16, sizeof(PollEntry)) == VECTOR_ERROR) {
		return ERROR;
	}
	if (vector_setup(other_fds, 16, sizeof(struct pollfd)) == VECTOR_ERROR) {
		return ERROR;
	}

	for (nfds_t index = 0; index < number_of_fds; ++index) {
		assert(fds[index].fd > 0);

		// This is necessary for repeated calls with the same poll structures
		// (the kernel probably does this internally first too)
		fds[index].revents = 0;

		Session* session = bridge_lookup(&bridge, fds[index].fd);
		if (session_has_connection(session)) {
			PollEntry entry;
			entry.connection = session->connection;
			entry.poll_pointer = &fds[index];
			vector_push_back(tssx_fds, &entry);
		} else {
			vector_push_back(other_fds, &fds[index]);
		}
	}

	return SUCCESS;
}

int _concurrent_poll(Vector* tssx_fds, Vector* other_fds, int timeout) {
	ready_count_t ready_count = ATOMIC_VAR_INIT(0);
	struct sigaction old_action;
	pthread_t other_thread;
	PollTask other_task = {other_fds, timeout, &ready_count};
	PollTask tssx_task = {tssx_task, timeout, &ready_count};

	if ((_install_poll_signal_handler(&old_action)) == ERROR) {
		return ERROR;
	}

	if (_start_other_poll_thread(&other_thread, &other_task) == ERROR) {
		return ERROR;
	}

	// Note: Will run in this thread, but deals with concurrent polling
	_concurrent_tssx_poll(&tssx_task, other_thread);

	// Theoretically not necessary because we synchronize either through
	// the timeout, or via a change on the ready count (quasi condition variable)
	// Although, note that POSIX requires a join to reclaim resources,
	// unless we detach the thread with pthread_detach to make it a daemon
	if (pthread_join(other_thread, NULL) != SUCCESS) {
		return ERROR;
	}
	_restore_old_signal_action(&old_action);

	// Three cases for the ready count
	// An error occurred in either polling, then it is -1.
	// The timeout expired, then it is 0.
	// Either poll found a change, then it is positive.

	return atomic_load(&ready_count);
}

int _start_other_poll_thread(pthread_t* poll_thread, PollTask* task) {
	thread_function_t function = (thread_function_t)_other_poll;

	if (pthread_create(poll_thread, NULL, function, task) != SUCCESS) {
		print_error("Error creating polling thread");
		return ERROR;
	}

	return SUCCESS;
}

void _other_poll(PollTask* task) {
	int local_ready_count = 0;
	struct pollfd* raw = task->fds->data;
	size_t size = task->fds->size;

	assert(task != NULL);
	assert(raw != NULL);

	local_ready_count = real_poll(raw, size, task->timeout);

	// Don't touch anything else if there was an error in the main thread
	if (_there_was_an_error(task->ready_count)) return;

	// Check if there was an error in real_poll, but not EINTR, which
	// would be the signal received when one or more TSSX fd was ready
	if (local_ready_count == ERROR) {
		if (errno != EINTR) {
			atomic_store(task->ready_count, ERROR);
		}
		return;
	}

	// Either there was a timeout (+= 0), or some FDs are ready
	atomic_fetch_add(task->ready_count, local_ready_count);
}

int _simple_tssx_poll(Vector* tssx_fds, int timeout) {
	size_t start = current_milliseconds();
	int ready_count = 0;

	// Do-while for the case of non-blocking (timeout == -1)
	// so that we do at least one iteration

	do {
		// Do a full loop over all FDs
		VECTOR_FOR_EACH(tssx_fds, iterator) {
			PollEntry* entry = iterator_get(&iterator);
			if (_check_ready(entry, READ) || _check_ready(entry, WRITE)) {
				++ready_count;
			}
		}

	} while (ready_count == 0 && !_poll_timeout_elapsed(start, timeout));

	return ready_count;
}

void _concurrent_tssx_poll(PollTask* task, pthread_t other_thread) {
	size_t global_ready_count;
	size_t start = current_milliseconds();
	size_t local_ready_count = 0;

	assert(task != NULL);
	assert(task->fds != NULL);

	// Do-while for the case of non-blocking (timeout == -1)
	// so that we do at least one iteration

	do {
		// Do a full loop over all FDs
		VECTOR_FOR_EACH(task->fds, iterator) {
			PollEntry* entry = iterator_get(&iterator);
			if (_check_ready(entry, READ) || _check_ready(entry, WRITE)) {
				++local_ready_count;
			}
		}

		global_ready_count = atomic_load(task->ready_count);

		// Don't touch if there was an error in the other thread
		if (global_ready_count == ERROR) return;
		if (global_ready_count > 0) break;
		if (local_ready_count > 0) {
			_kill_other_thread(other_thread);
			break;
		}
	} while (!_poll_timeout_elapsed(start, task->timeout));

	// Add whatever we have (zero if the timeout elapsed)
	atomic_fetch_add(task->ready_count, local_ready_count);
}


bool _check_ready(PollEntry* entry, Operation operation) {
	assert(entry != NULL);
	if (_waiting_for(entry, operation)) {
		// This here is the polymorphic call (see client/server-overrides)
		if (_ready_for(entry->connection, operation)) {
			_tell_that_ready_for(entry, operation);
			return true;
		}
	}

	return false;
}

bool _waiting_for(PollEntry* entry, Operation operation) {
	assert(entry != NULL);
	assert(entry->poll_pointer != NULL);
	return entry->poll_pointer->events & _operation_map[operation];
}

bool _tell_that_ready_for(PollEntry* entry, Operation operation) {
	return entry->poll_pointer->revents |= _operation_map[operation];
}

void _cleanup(Vector* tssx_fds, Vector* other_fds) {
	vector_destroy(tssx_fds);
	vector_destroy(other_fds);
}
