#define _GNU_SOURCE

#include <assert.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signal.h>

#include "tssx/bridge.h"
#include "tssx/connection.h"
#include "tssx/common-poll-overrides.h"
#include "tssx/poll-overrides.h"
#include "tssx/select-overrides.h"
#include "utility/utility.h"

/******************** REAL FUNCTION ********************/

int real_select(int nfds,
								fd_set* readfds,
								fd_set* writefds,
								fd_set* errorfds,
								struct timeval* timeout) {
	// clang-format off
	return ((real_select_t)dlsym(RTLD_NEXT, "select"))
            (nfds, readfds, writefds, errorfds, timeout);
	// clang-format on
}

int real_pselect(int nfds,
								 fd_set* readfds,
								 fd_set* writefds,
								 fd_set* errorfds,
								 const struct timespec* timeout,
								 const sigset_t* sigmask) {
	// clang-format off
	return ((real_pselect_t)dlsym(RTLD_NEXT, "pselect"))
            (nfds, readfds, writefds, errorfds, timeout, sigmask);
	// clang-format on
}

/******************** OVERRIDES ********************/

int select(int nfds,
					 fd_set* readfds,
					 fd_set* writefds,
					 fd_set* errorfds,
					 struct timeval* timeout) {
	DescriptorSets sets = {readfds, writefds, errorfds};
	size_t tssx_count, normal_count, lowest_fd;
	_count_tssx_sockets(nfds, &sets, &lowest_fd, &normal_count, &tssx_count);

	if (normal_count == 0) {
		return _select_on_tssx_only(&sets, tssx_count, lowest_fd, nfds, timeout);
	} else if (tssx_count == 0) {
		return real_select(nfds, readfds, writefds, errorfds, timeout);
	} else {
		return _forward_to_poll(nfds, &sets, tssx_count + normal_count, timeout);
	}
}

int pselect(int nfds,
						fd_set* readfds,
						fd_set* writefds,
						fd_set* errorfds,
						const struct timespec* timeout,
						const sigset_t* sigmask) {
	struct timeval timeval_timeout;
	struct timeval* timeval_pointer;
	sigset_t original_mask;
	int event_count;

	if (_lazy_select_setup() == ERROR) return ERROR;

	if (timeout == NULL) {
		// Block indefinitely
		timeval_pointer = NULL;
	} else {
		timespec_to_timeval(timeout, &timeval_timeout);
		timeval_pointer = &timeval_timeout;
	}

	if (sigmask == NULL) {
		return select(nfds, readfds, writefds, errorfds, timeval_pointer);
	}

	if (_set_poll_mask(&_select_lock, sigmask, &original_mask) == ERROR) return ERROR;

	event_count = select(nfds, readfds, writefds, errorfds, timeval_pointer);

	if (_restore_poll_mask(&_select_lock, &original_mask) == ERROR) return ERROR;

	return event_count;
}

/******************** PRIVATE DEFINITIONS ********************/

pthread_mutex_t _select_lock = PTHREAD_MUTEX_INITIALIZER;
bool _select_is_initialized = false;

/******************** HELPERS ********************/

int _forward_to_poll(size_t highest_fd,
										 DescriptorSets* sets,
										 size_t population_count,
										 struct timeval* timeout) {
	struct pollfd* poll_entries;
	size_t number_of_events;
	int milliseconds;

	poll_entries = _setup_poll_entries(population_count, sets, highest_fd);
	if (poll_entries == NULL) {
		return ERROR;
	}

	milliseconds = timeout ? timeval_to_milliseconds(timeout) : BLOCK_FOREVER;

	// The actual forwarding call
	number_of_events = poll(poll_entries, population_count, milliseconds);

	if (number_of_events == ERROR) {
		free(poll_entries);
		return ERROR;
	}

	if (_read_poll_entries(sets, poll_entries, population_count) == ERROR) {
		return ERROR;
	}

	return number_of_events;
}

struct pollfd* _setup_poll_entries(size_t population_count,
																	 const DescriptorSets* sets,
																	 size_t highest_fd) {
	struct pollfd* poll_entries;

	poll_entries = calloc(population_count, sizeof *poll_entries);
	if (poll_entries == NULL) {
		perror("Error allocating memory for poll entries");
		return NULL;
	}

	_fill_poll_entries(poll_entries, sets, highest_fd);

	return poll_entries;
}

int _read_poll_entries(DescriptorSets* sets,
											 struct pollfd* poll_entries,
											 size_t population_count) {
	// First unset all, then just repopulate
	_clear_all_sets(sets);

	for (size_t index = 0; index < population_count; ++index) {
		struct pollfd* entry = &(poll_entries[index]);

		if (_check_poll_event_occurred(entry, sets, POLLNVAL)) continue;
		_check_poll_event_occurred(entry, sets, POLLIN);
		_check_poll_event_occurred(entry, sets, POLLOUT);
		_check_poll_event_occurred(entry, sets, POLLERR);
	}

	free(poll_entries);

	return SUCCESS;
}

void _fill_poll_entries(struct pollfd* poll_entries,
												const DescriptorSets* sets,
												size_t highest_fd) {
	size_t poll_index = 0;

	for (size_t fd = 0; fd < highest_fd; ++fd) {
		if (!_is_in_any_set(fd, sets)) continue;

		poll_entries[poll_index].fd = fd;

		if (_fd_is_set(fd, sets->readfds)) {
			poll_entries[poll_index].events |= POLLIN;
		}
		if (_fd_is_set(fd, sets->writefds)) {
			poll_entries[poll_index].events |= POLLOUT;
		}
		if (_fd_is_set(fd, sets->errorfds)) {
			poll_entries[poll_index].events |= POLLERR;
		}

		++poll_index;
	}
}

int _select_on_tssx_only(DescriptorSets* sets,
												 size_t tssx_count,
												 size_t lowest_fd,
												 size_t highest_fd,
												 struct timeval* timeout) {
	if (tssx_count == 1) {
		assert(lowest_fd + 1 == highest_fd);
		return _select_on_tssx_only_fast_path(sets, lowest_fd, timeout);
	}

	size_t start = current_milliseconds();
	int ready_count = 0;
	int milliseconds = timeout ? timeval_to_milliseconds(timeout) : BLOCK_FOREVER;

	fd_set readfds, writefds, errorfds;
	DescriptorSets original = {&readfds, &writefds, &errorfds};
	_copy_all_sets(&original, sets);
	_clear_all_sets(sets);

	// Do-while for the case of non-blocking
	// so that we do at least one iteration
	do {
		for (size_t fd = lowest_fd; fd < highest_fd; ++fd) {
			Session* session = bridge_lookup(&bridge, fd);
			if (_check_select_events(fd, session, &original)) ++ready_count;
		}
		if (ready_count > 0) break;
	} while (!_select_timeout_elapsed(start, milliseconds));

	return ready_count;
}

int _select_on_tssx_only_fast_path(DescriptorSets* sets,
																	 size_t fd,
																	 struct timeval* timeout) {
	bool ready;
	bool select_read = _fd_is_set(fd, sets->readfds);
	bool select_write = _fd_is_set(fd, sets->writefds);
	assert(select_read || select_write);

	_clear_all_sets(sets);

	size_t start = current_milliseconds();
	int milliseconds = timeout ? timeval_to_milliseconds(timeout) : BLOCK_FOREVER;

	Session* session = bridge_lookup(&bridge, fd);

	ready = false;
	if (select_read && select_write) {
		do {
			if (_select_peer_died(fd, session, sets)) return true;
			if (_ready_for_select(fd, session, sets->writefds, WRITE)) ready = true;
			if (_ready_for_select(fd, session, sets->readfds, READ)) ready = true;
		} while (!ready && !_select_timeout_elapsed(start, milliseconds));
	} else if (select_read) {
		do {
			if (_select_peer_died(fd, session, sets)) return true;
			if (_ready_for_select(fd, session, sets->readfds, READ)) return true;
		} while (!_select_timeout_elapsed(start, milliseconds));
	} else {
		do {
			if (_select_peer_died(fd, session, sets)) return true;
			if (_ready_for_select(fd, session, sets->writefds, WRITE)) return true;
		} while (!_select_timeout_elapsed(start, milliseconds));
	}

	return ready;
}

void _count_tssx_sockets(size_t highest_fd,
												 const DescriptorSets* sets,
												 size_t* lowest_fd,
												 size_t* normal_count,
												 size_t* tssx_count) {
	*normal_count = 0;
	*tssx_count = 0;
	*lowest_fd = highest_fd;

	for (size_t fd = 0; fd < highest_fd; ++fd) {
		if (_is_in_any_set(fd, sets)) {
			if (fd < *lowest_fd) {
				*lowest_fd = fd;
			}
			if (bridge_has_connection(&bridge, fd)) {
				++(*tssx_count);
			} else {
				++(*normal_count);
			}
		}
	}
}

void _copy_all_sets(DescriptorSets* destination, const DescriptorSets* source) {
	_copy_set(destination->readfds, source->readfds);
	_copy_set(destination->writefds, source->writefds);
	_copy_set(destination->errorfds, source->errorfds);
}

void _copy_set(fd_set* destination, const fd_set* source) {
	if (source == NULL) {
		FD_ZERO(destination);
	} else {
		*destination = *source;
	}
}

void _clear_all_sets(DescriptorSets* sets) {
	_clear_set(sets->readfds);
	_clear_set(sets->writefds);
	_clear_set(sets->errorfds);
}

bool _is_in_any_set(int fd, const DescriptorSets* sets) {
	if (_fd_is_set(fd, sets->readfds)) return true;
	if (_fd_is_set(fd, sets->writefds)) return true;
	if (_fd_is_set(fd, sets->errorfds)) return true;

	return false;
}

bool _fd_is_set(int fd, const fd_set* set) {
	return set != NULL && FD_ISSET(fd, set);
}

bool _select_timeout_elapsed(size_t start, int timeout) {
	if (timeout == BLOCK_FOREVER) return false;
	if (timeout == DONT_BLOCK) return true;

	return (current_milliseconds() - start) > timeout;
}

void _clear_set(fd_set* set) {
	if (set != NULL) FD_ZERO(set);
}

bool _check_select_events(int fd, Session* session, DescriptorSets* sets) {
	bool activity = false;

	assert(session != NULL);
	assert(sets != NULL);

	if (_select_peer_died(fd, session, sets)) return true;

	if (_waiting_and_ready_for_select(fd, session, sets, READ)) {
		activity = true;
	}

	if (_waiting_and_ready_for_select(fd, session, sets, WRITE)) {
		activity = true;
	}

	return activity;
}

bool _select_peer_died(int fd, Session* session, DescriptorSets* sets) {
	if (connection_peer_died(session->connection)) {
		// When the connection hangs up (open-count drops to one)
		// a read event should be triggered, as read() will return zero
		FD_SET(fd, sets->readfds);
		return true;
	}

	return false;
}

bool _waiting_and_ready_for_select(int fd,
																	 Session* session,
																	 const DescriptorSets* sets,
																	 Operation operation) {
	fd_set* set;

	set = _fd_set_for_operation(sets, operation);
	if (!_fd_is_set(fd, set)) return false;

	return _ready_for_select(fd, session, set, operation);
}

bool _ready_for_select(int fd,
											 Session* session,
											 fd_set* set,
											 Operation operation) {
	if (_ready_for(session->connection, operation)) {
		FD_SET(fd, set);
		return true;
	}

	return false;
}

bool _check_poll_event_occurred(const struct pollfd* entry,
																DescriptorSets* sets,
																int event) {
	fd_set* set;

	assert(entry != NULL);
	assert(sets != NULL);
	assert(event == POLLIN || event == POLLOUT || event == POLLERR ||
				 event == POLLNVAL);

	if (entry->revents & event) {
		set = _fd_set_for_poll_event(sets, event);
		FD_SET(entry->fd, set);
		return true;
	}

	return false;
}

fd_set* _fd_set_for_operation(const DescriptorSets* sets, Operation operation) {
	return (operation == READ) ? sets->readfds : sets->writefds;
}

fd_set* _fd_set_for_poll_event(const DescriptorSets* sets, int poll_event) {
	switch (poll_event) {
		case POLLNVAL: return sets->errorfds;
		case POLLIN: return sets->readfds;
		case POLLOUT: return sets->writefds;
		case POLLERR: return sets->errorfds;
		default: assert(false); return NULL;
	}
}

int _lazy_select_setup() {
	if (!_select_is_initialized) {
		return _setup_select();
	}

	return SUCCESS;
}

int _setup_select() {
	assert(!_select_is_initialized);

	if (atexit(_destroy_select_lock) == ERROR) {
		print_error("Error registering destructor with atexit() in select\n");
		return ERROR;
	}

	_select_is_initialized = true;

	return SUCCESS;
}

void _destroy_select_lock() {
    pthread_mutex_unlock(&_select_lock);
    pthread_mutex_destroy(&_select_lock);
}
