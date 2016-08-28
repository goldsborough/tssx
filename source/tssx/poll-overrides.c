#define _GNU_SOURCE

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>

#include "tssx/bridge.h"
#include "tssx/connection.h"
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
	Vector tssx_fds, normal_fds;
	int event_count;
	puts("adsfasfsdf\n");

	if (nfds == 0) return 0;

	puts("!!\n");
	if (_partition(&tssx_fds, &normal_fds, fds, nfds) == ERROR) {
		return ERROR;
	}
	puts("?\n");

	if (tssx_fds.size == 0) {
	  puts("normal\n");
		// We are only dealing with normal (non-tssx) fds
		event_count = real_poll(fds, nfds, timeout);
	} else if (normal_fds.size == 0) {
	  puts("tssx\n");
		// We are only dealing with tssx connections
		event_count = _simple_tssx_poll(&tssx_fds, timeout);
	} else {
	  puts("123\n");
		event_count = _concurrent_poll(&tssx_fds, &normal_fds, timeout);
			  puts("124\n");
		_join_poll_partition(fds, nfds, &normal_fds);
			  puts("125\n");
	}

	_cleanup(&tssx_fds, &normal_fds);

	return event_count;
}

/******************** PRIVATE DEFINITIONS ********************/

const short _operation_map[2] = {POLLIN, POLLOUT};

pthread_cond_t _poll_condition = PTHREAD_COND_INITIALIZER;
pthread_mutex_t _poll_lock = PTHREAD_MUTEX_INITIALIZER;

bool _normal_thread_ready = false;
bool _poll_is_initialized = false;

/******************** HELPERS ********************/

int _partition(Vector* tssx_fds,
							 Vector* normal_fds,
							 struct pollfd fds[],
							 nfds_t number_of_fds) {
	// Minimum capacity of 16 each
	if (vector_setup(tssx_fds, 16, sizeof(PollEntry)) == VECTOR_ERROR) {
		return ERROR;
	}
	if (vector_setup(normal_fds, 16, sizeof(struct pollfd)) == VECTOR_ERROR) {
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
			vector_push_back(normal_fds, &fds[index]);
		}
	}

	return SUCCESS;
}

void _join_poll_partition(struct pollfd fds[],
													nfds_t number_of_fds,
													Vector* normal_fds) {
	size_t normal_index = 0;

	for (nfds_t index = 0; index < number_of_fds; ++index) {
		Session* session;
		struct pollfd* entry;

		session = bridge_lookup(&bridge, fds[index].fd);
		if (session_has_connection(session)) continue;

		entry = vector_get(normal_fds, normal_index);
		assert(entry->fd == fds[index].fd);
		fds[index].revents = entry->revents;
		++normal_index;
	}
}

int _concurrent_poll(Vector* tssx_fds, Vector* normal_fds, int timeout) {
	event_count_t event_count = ATOMIC_VAR_INIT(0);
	struct sigaction old_action;
	pthread_t normal_thread;
	PollTask normal_task = {normal_fds, timeout, &event_count};
	PollTask tssx_task = {tssx_fds, timeout, &event_count};

	if ((_install_poll_signal_handler(&old_action)) == ERROR) return ERROR;

	_normal_thread_ready = false;

	if (_start_normal_poll_thread(&normal_thread, &normal_task) == ERROR) {
		return ERROR;
	}

	// We need synchronization because it may otherwise occur that the
	// main thread (for TSSX) finds an event before the other thread even started
	// (or at least started polling). It would then send the kill signal before
	// the other thread can catch it. If the other thread has some fds that
	// happen to not signal, and has an indefinite timeout, then joining below
	// will block forever. Thus we need to make sure the other thread is ready to
	// be signalled.
	if (_wait_for_normal_thread() == ERROR) return ERROR;

	puts("10\n");

	// Note: Will run in this thread, but deals with concurrent polling
	_concurrent_tssx_poll(&tssx_task, normal_thread);
	puts("11\n");
	// Theoretically not necessary because we synchronize either through
	// the timeout, or via a change on the ready count (quasi condition variable)
	// Although, note that POSIX requires a join to reclaim resources,
	// unless we detach the thread with pthread_detach to make it a daemon
	if (pthread_join(normal_thread, NULL) != SUCCESS) return ERROR;
	puts("12\n");
	_restore_old_signal_action(&old_action);
		puts("13\n");

	// Three cases for the ready count
	// An error occurred in either polling, then it is -1.
	// The timeout expired, then it is 0.
	// Either poll found a change, then it is positive.

	return atomic_load(&event_count);
}

int _start_normal_poll_thread(pthread_t* poll_thread, PollTask* task) {
	thread_function_t function = (thread_function_t)_normal_poll;

	if (pthread_create(poll_thread, NULL, function, task) != SUCCESS) {
		print_error("Error creating polling thread");
		return ERROR;
	}

	return SUCCESS;
}

void _normal_poll(PollTask* task) {
	int local_event_count = 0;
	struct pollfd* raw = task->fds->data;
	size_t size = task->fds->size;

	assert(task != NULL);
	assert(raw != NULL);

	// Tell the main thread that it can start polling now
	if (_signal_tssx_thread() == ERROR) {
		atomic_store(task->event_count, ERROR);
		return;
	}

	// Only start polling if in the mean time the other thread
	// didn't already find events (and we missed the signal) or
	// had an error occur, in which case we also don't want to poll
	if (atomic_load(task->event_count) == 0) {
		local_event_count = real_poll(raw, size, task->timeout);
	}

	// Don't touch anything else if there was an error in the main thread
	if (_there_was_an_error(task->event_count)) return;

	// Check if there was an error in real_poll, but not EINTR, which
	// would be the signal received when one or more TSSX fd was ready
	if (local_event_count == ERROR) {
		if (errno != EINTR) {
			atomic_store(task->event_count, ERROR);
		}
		return;
	}

	// Either there was a timeout (+= 0), or some FDs are ready
	atomic_fetch_add(task->event_count, local_event_count);
}

int _simple_tssx_poll(Vector* tssx_fds, int timeout) {
	size_t start = current_milliseconds();
	int event_count = 0;

	// Do-while for the case of non-blocking (timeout == -1)
	// so that we do at least one iteration

	do {
		// Do a full loop over all FDs
		VECTOR_FOR_EACH(tssx_fds, iterator) {
			PollEntry* entry = iterator_get(&iterator);
			if (_check_poll_events(entry)) ++event_count;
		}
		if (event_count > 0) break;
	} while (!_poll_timeout_elapsed(start, timeout));

	return event_count;
}

void _concurrent_tssx_poll(PollTask* task, pthread_t normal_thread) {
	size_t shared_event_count;
	size_t start = current_milliseconds();
	size_t local_event_count = 0;

	assert(task != NULL);
	assert(task->fds != NULL);

	// Do-while for the case of non-blocking (timeout == -1)
	// so that we do at least one iteration

	do {
		// Do a full loop over all FDs
		VECTOR_FOR_EACH(task->fds, iterator) {
			PollEntry* entry = iterator_get(&iterator);
			if (_check_poll_events(entry)) ++local_event_count;
		}

		shared_event_count = atomic_load(task->event_count);

		// Don't touch if there was an error in the normal thread
		if (shared_event_count == ERROR) return;
		if (shared_event_count > 0) break;
		if (local_event_count > 0) {
			_kill_normal_thread(normal_thread);
			break;
		}
	} while (!_poll_timeout_elapsed(start, task->timeout));

	// Add whatever we have (zero if the timeout elapsed)
	atomic_fetch_add(task->event_count, local_event_count);
}

bool _check_poll_events(PollEntry* entry) {
	bool event_occurred = false;

	if (_entry_peer_died(entry)) {
		// Note that POLLHUP is output only and ignored in events,
		// meaning we must always set it, irrespective of it being
		// expected by the user in pollfd.events
		entry->poll_pointer->revents |= POLLHUP;

		// If the connection peer died, a call to read() will return
		// zero, therefore a hangup event is also a read event
		_tell_that_ready_for(entry, READ);

		return true;
	}

	if (_check_ready(entry, READ)) event_occurred = true;
	if (_check_ready(entry, WRITE)) event_occurred = true;

	return event_occurred;
}


bool _check_ready(PollEntry* entry, Operation operation) {
	assert(entry != NULL);
	if (_waiting_for(entry, operation)) {
		// _ready_for here is the polymorphic call (see client/server-overrides)
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

bool _entry_peer_died(PollEntry* entry) {
	assert(entry != NULL);
	return connection_peer_died(entry->connection);
}

void _cleanup(Vector* tssx_fds, Vector* normal_fds) {
	vector_destroy(tssx_fds);
	vector_destroy(normal_fds);
}

int _lazy_poll_setup() {
	if (!_poll_is_initialized) {
		return _setup_poll();
	}
	return SUCCESS;
}

int _setup_poll() {
	assert(!_poll_is_initialized);

	if (atexit(_destroy_poll_lock_and_condvar) == ERROR) {
		print_error("Error registering destructor with atexit() in poll\n");
		return ERROR;
	}

	_poll_is_initialized = true;

	return SUCCESS;
}

void _destroy_poll_lock_and_condvar() {
    pthread_mutex_unlock(&_poll_lock);
    
	if (pthread_mutex_destroy(&_poll_lock) != SUCCESS) {
		print_error("Error destroying mutex\n");
	}

	if (pthread_cond_destroy(&_poll_condition) != SUCCESS) {
		print_error("Error destroying condition variable\n");
	}
}

int _signal_tssx_thread() {
	if (pthread_mutex_lock(&_poll_lock) != SUCCESS) {
		print_error("Error locking mutex\n");
		return ERROR;
	}

	_normal_thread_ready = true;

	if (pthread_cond_signal(&_poll_condition) != SUCCESS) {
		print_error("Error signalling main (tssx) thread\n");
		return ERROR;
	}

	if (pthread_mutex_unlock(&_poll_lock) != SUCCESS) {
		print_error("Error unlocking mutex\n");
		return ERROR;
	}

	return SUCCESS;
}

int _wait_for_normal_thread() {
	if (pthread_mutex_lock(&_poll_lock) != SUCCESS) {
		print_error("Error locking mutex\n");
		return ERROR;
	}

	while (!_normal_thread_ready) {
		if (pthread_cond_wait(&_poll_condition, &_poll_lock) != SUCCESS) {
			print_error("Error waiting for condition in poll\n");
			return ERROR;
		}
	}

	if (pthread_mutex_unlock(&_poll_lock) != SUCCESS) {
		print_error("Error unlocking mutex\n");
		return ERROR;
	}

	return SUCCESS;
}
