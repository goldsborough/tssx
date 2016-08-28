#define _GNU_SOURCE

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "tssx/bridge.h"
#include "tssx/connection.h"
#include "tssx/definitions.h"
#include "tssx/epoll-overrides.h"
#include "tssx/session.h"
#include "utility/common.h"

/******************** REAL FUNCTIONS ********************/

int real_epoll_create(int size) {
	return ((real_epoll_create_t)dlsym(RTLD_NEXT, "epoll_create"))(size);
}

int real_epoll_create1(int flags) {
	return ((real_epoll_create1_t)dlsym(RTLD_NEXT, "epoll_create1"))(flags);
}

int real_epoll_ctl(int epfd, int operation, int fd, struct epoll_event *event) {
	// clang-format off
	return ((real_epoll_ctl_t)dlsym(RTLD_NEXT, "epoll_ctl"))
            (epfd, operation, fd, event);
	// clang-format on
}

int real_epoll_wait(int epfd,
										struct epoll_event *events,
										int number_of_events,
										int timeout) {
	// clang-format off
  return ((real_epoll_wait_t)dlsym(RTLD_NEXT, "epoll_wait"))
                (epfd, events, number_of_events, timeout);
	// clang-format on
}

int real_epoll_pwait(int epfd,
										 struct epoll_event *events,
										 int number_of_events,
										 int timeout,
										 const sigset_t *sigmask) {
	// clang-format off
  return ((real_epoll_pwait_t)dlsym(RTLD_NEXT, "epoll_pwait"))
            (epfd, events, number_of_events, timeout, sigmask);
	// clang-format on
}

/******************** OVERRIDES ********************/

int epoll_create(int size) {
	return epoll_create1(0);
}

int epoll_create1(int flags) {
	int epfd;

	if (_lazy_epoll_setup() == ERROR) return ERROR;

	if ((epfd = real_epoll_create1(flags)) == ERROR) {
		return ERROR;
	}

	assert(!has_epoll_instance_associated(epfd));

	return epfd;
}

int epoll_ctl(int epfd, int operation, int fd, struct epoll_event *event) {
	Session *session;

	assert(_epoll_instances_are_initialized);
	if (_lazy_epoll_setup_and_error()) return ERROR;

	if (fd < 0) {
		errno = EINVAL;
		return ERROR;
	}

	session = bridge_lookup(&bridge, fd);
	if (session_has_connection(session)) {
		return _epoll_tssx_control_operation(epfd, operation, fd, event, session);
	} else {
		return _epoll_normal_control_operation(epfd, operation, fd, event);
	}
}

int epoll_wait(int epfd,
							 struct epoll_event *events,
							 int number_of_events,
							 int timeout) {
	EpollInstance *instance;

	assert(_epoll_instances_are_initialized);
	if (_lazy_epoll_setup_and_error()) return ERROR;

	if (_validate_epoll_wait_arguments(epfd, number_of_events) == ERROR) {
		return ERROR;
	}

	instance = &_epoll_instances[epfd];

	if (instance->tssx_count == 0) {
		return real_epoll_wait(epfd, events, number_of_events, timeout);
	} else if (instance->normal_count == 0) {
		return _simple_tssx_epoll_wait(instance, events, number_of_events, timeout);
	} else {
		return _concurrent_epoll_wait(epfd, events, number_of_events, timeout);
	}
}

int epoll_pwait(int epfd,
								struct epoll_event *events,
								int number_of_events,
								int timeout,
								const sigset_t *sigmask) {
	sigset_t original_mask;
	int event_count;

	if (sigmask == NULL) {
		return epoll_wait(epfd, events, number_of_events, timeout);
	}

	if (pthread_mutex_lock(&_epoll_lock) != SUCCESS) {
		return ERROR;
	}

	if (pthread_sigmask(SIG_SETMASK, sigmask, &original_mask) != SUCCESS) {
		return ERROR;
	}

	event_count = epoll_wait(epfd, events, number_of_events, timeout);

	if (pthread_sigmask(SIG_SETMASK, &original_mask, NULL) != SUCCESS) {
		return ERROR;
	}

	if (pthread_mutex_unlock(&_epoll_lock) != SUCCESS) {
		return ERROR;
	}

	return event_count;
}

bool has_epoll_instance_associated(int epfd) {
	assert(epfd > 0);
	return epoll_instance_size(epfd) > 0;
}

size_t epoll_instance_size(int epfd) {
	assert(epfd > 2);
	assert(epfd < NUMBER_OF_EPOLL_INSTANCES);
	assert(_epoll_instances_are_initialized);

	return _epoll_instances[epfd].tssx_count +
				 _epoll_instances[epfd].normal_count;
}

int close_epoll_instance(int epfd) {
	EpollInstance *instance;

	assert(epfd > 0);
	assert(epfd < NUMBER_OF_EPOLL_INSTANCES);

	instance = &_epoll_instances[epfd];

	if (instance->tssx_count > 1) {
		if (vector_destroy(&instance->entries) == VECTOR_ERROR) {
			return ERROR;
		}
	}

	instance->tssx_count = 0;
	instance->normal_count = 0;

	// close() calls real_close() on the actual file descriptor

	return SUCCESS;
}

/******************** PRIVATE DEFINITIONS ********************/

// clang-format off
epoll_operation_t _epoll_operation_map[2] = {
  EPOLLIN,
  EPOLLOUT
};
// clang-format on

EpollInstance _epoll_instances[NUMBER_OF_EPOLL_INSTANCES];
bool _epoll_instances_are_initialized = false;

pthread_mutex_t _epoll_lock = PTHREAD_MUTEX_INITIALIZER;

/******************** PRIVATE ********************/

int _setup_epoll_instances() {
	assert(!_epoll_instances_are_initialized);

	for (size_t instance = 0; instance < NUMBER_OF_EPOLL_INSTANCES; ++instance) {
		_epoll_instances[instance].tssx_count = 0;
		_epoll_instances[instance].normal_count = 0;
	}

	if (atexit(_destroy_epoll_lock) == ERROR) {
		print_error("Error registering destructor with atexit() in epoll\n");
		return ERROR;
	}

	_epoll_instances_are_initialized = true;

	return SUCCESS;
}

int _epoll_normal_control_operation(int epfd,
																		int operation,
																		int fd,
																		struct epoll_event *event) {
	if (operation == EPOLL_CTL_ADD) {
		_epoll_instances[epfd].normal_count++;
	} else if (operation == EPOLL_CTL_DEL) {
		assert(_epoll_instances[epfd].normal_count > 0);
		_epoll_instances[epfd].normal_count--;
	}

	return real_epoll_ctl(epfd, operation, fd, event);
}

int _epoll_tssx_control_operation(int epfd,
																	int operation,
																	int fd,
																	struct epoll_event *event,
																	Session *session) {
	EpollInstance *instance = &_epoll_instances[epfd];
	// clang-format off
	switch (operation) {
		case EPOLL_CTL_ADD:
			return _epoll_add_to_instance(instance, fd, event, session);
			break;
		case EPOLL_CTL_MOD:
     return _epoll_update_instance(instance, fd, event);
     break;
		case EPOLL_CTL_DEL:
			return _epoll_erase_from_instance(instance, fd);
			break;
		default:
			errno = EINVAL;
			print_error("Invalid epoll operation\n");
			return ERROR;
	}
	// clang-format on
}

int _epoll_add_to_instance(EpollInstance *instance,
													 int fd,
													 struct epoll_event *event,
													 Session *session) {
	if (instance->tssx_count == 0) {
		return _epoll_set_first_entry(instance, fd, event, session);
	} else {
		return _epoll_push_back_entry(instance, fd, event, session);
	}
}

int _epoll_set_first_entry(EpollInstance *instance,
													 int fd,
													 struct epoll_event *event,
													 Session *session) {
	instance->first.fd = fd;
	instance->first.event = *event;
	instance->first.connection = session->connection;
	instance->first.flags = ENABLED;

	instance->tssx_count++;

	return SUCCESS;
}

int _epoll_push_back_entry(EpollInstance *instance,
													 int fd,
													 struct epoll_event *event,
													 Session *session) {
	EpollEntry entry = {fd, *event, session->connection, ENABLED};

	if (instance->tssx_count == 1) {
		// clang-format off
		int code = vector_setup(
        &instance->entries,
        INITIAL_INSTANCE_CAPACITY,
        sizeof(EpollEntry)
    );
		// clang-format on

		if (code == VECTOR_ERROR) return ERROR;
	}

	if (vector_push_back(&instance->entries, &entry) == VECTOR_ERROR) {
		return ERROR;
	}

	instance->tssx_count++;

	return SUCCESS;
}

int _epoll_update_instance(EpollInstance *instance,
													 int fd,
													 const struct epoll_event *new_event) {
	assert(instance->tssx_count > 0);

	if (fd == instance->first.fd) {
		instance->first.event = *new_event;
		instance->first.flags = ENABLED;
	} else {
		EpollEntry *entry = _find_epoll_entry(instance, fd);

		if (entry == NULL) {
			_invalid_argument_exception();
			return ERROR;
		}

		entry->event = *new_event;
		entry->flags = ENABLED;
	}

	return SUCCESS;
}

EpollEntry *_find_epoll_entry(EpollInstance *instance, int fd) {
	VECTOR_FOR_EACH(&instance->entries, iterator) {
		EpollEntry *entry = iterator_get(&iterator);
		if (entry->fd == fd) return entry;
	}

	return NULL;
}

int _epoll_erase_from_instance(EpollInstance *instance, int fd) {
	assert(instance->tssx_count > 0);

	if (fd == instance->first.fd) {
		return _epoll_erase_first_from_instance(instance);
	}

	VECTOR_FOR_EACH(&instance->entries, iterator) {
		EpollEntry *entry = iterator_get(&iterator);
		if (entry->fd == fd) {
			if (iterator_erase(&instance->entries, &iterator) == VECTOR_ERROR) {
				return ERROR;
			}

			instance->tssx_count--;
			return SUCCESS;
		}
	}

	_invalid_argument_exception();

	return ERROR;
}

int _epoll_erase_first_from_instance(EpollInstance *instance) {
	EpollEntry *entry;

	// If we only have a single tssx fd, then it is "first"
	// so we can just return and decrement the tssx count
	if (instance->tssx_count > 1) {
		if ((entry = (EpollEntry *)vector_back(&instance->entries)) == NULL) {
			return ERROR;
		}

		instance->first = *entry;

		if (vector_pop_back(&instance->entries) == VECTOR_ERROR) {
			return ERROR;
		}
	}

	instance->tssx_count--;

	return SUCCESS;
}

int _simple_tssx_epoll_wait(EpollInstance *instance,
														struct epoll_event *events,
														size_t number_of_events,
														int timeout) {
	size_t event_count = 0;
	size_t start = current_milliseconds();
	EpollEntry *first = &instance->first;

	assert(instance->tssx_count > 0);

	if (instance->tssx_count == 1) {
		// clang-format off
		return _tssx_epoll_wait_for_single_entry(
      &instance->first,
      events,
      number_of_events,
      timeout
    );
		// clang-format on
	}

	do {
		if (_check_epoll_entry(first, events, number_of_events, event_count)) {
			++event_count;
		}

		VECTOR_FOR_EACH(&instance->entries, iterator) {
			EpollEntry *entry = iterator_get(&iterator);
			if (_check_epoll_entry(entry, events, number_of_events, event_count)) {
				++event_count;
			}
		}
	} while (event_count == 0 && !_poll_timeout_elapsed(start, timeout));

	return event_count;
}

int _concurrent_epoll_wait(int epfd,
													 struct epoll_event *events,
													 size_t number_of_events,
													 int timeout) {
	struct sigaction old_action;
	pthread_t normal_thread;
	event_count_t event_count = ATOMIC_VAR_INIT(0);
	struct epoll_event *tssx_events;
	size_t tssx_event_count;

	// clang-format off
  EpollTask normal_task = {
    epfd, events, number_of_events, timeout, &event_count, 0
  };
	// clang-format on

	tssx_events = calloc(number_of_events, sizeof(struct epoll_event));

	if (_install_poll_signal_handler(&old_action) == ERROR) {
		return ERROR;
	}

	if (_start_normal_epoll_wait_thread(&normal_thread, &normal_task) == ERROR) {
		return ERROR;
	}

	// clang-format off
	tssx_event_count = _concurrent_tssx_epoll_wait(
      &_epoll_instances[epfd],
      tssx_events,
      number_of_events,
      normal_thread,
      &event_count,
      timeout
  );
	// clang-format on

	if (pthread_join(normal_thread, NULL) == ERROR) {
		free(tssx_events);
		return ERROR;
	}

	if (_there_was_an_error(&event_count)) {
		free(tssx_events);
		return ERROR;
	}

	// Copy over
	for (size_t index = 0; index < tssx_event_count; ++index) {
		events[normal_task.event_count + index] = tssx_events[index];
	}

	free(tssx_events);
	_restore_old_signal_action(&old_action);

	assert(tssx_event_count + normal_task.event_count ==
				 atomic_load(&event_count));

	return atomic_load(&event_count);
}

int _start_normal_epoll_wait_thread(pthread_t *normal_thread, EpollTask *task) {
	thread_function_t function = (thread_function_t)_normal_epoll_wait;

	if (pthread_create(normal_thread, NULL, function, task) != SUCCESS) {
		print_error("Error creating epoll thread");
		return ERROR;
	}

	return SUCCESS;
}

void _normal_epoll_wait(EpollTask *task) {
	assert(task != NULL);

	// clang-format off
	task->event_count = real_epoll_wait(
      task->epfd,
      task->events,
      task->number_of_events,
      task->timeout
  );
	// clang-format on

	// Don't touch anything else if there was an error in the main thread
	if (_there_was_an_error(task->shared_event_count)) return;

	// Check if there was an error in real_epoll, but not EINTR, which
	// would be the signal received when one or more TSSX fd was ready
	if (task->event_count == ERROR) {
		if (errno != EINTR) {
			atomic_store(task->shared_event_count, ERROR);
		}
		return;
	}

	// Either there was a timeout (+= 0), or some FDs are ready
	atomic_fetch_add(task->shared_event_count, task->event_count);
}

int _concurrent_tssx_epoll_wait(EpollInstance *instance,
																struct epoll_event *events,
																size_t number_of_events,
																pthread_t normal_thread,
																event_count_t *shared_event_count,
																int timeout) {
	size_t shared_event_count_value;
	size_t start = current_milliseconds();
	size_t event_count = 0;
	EpollEntry *first = &instance->first;

	assert(instance != NULL);
	assert(instance->tssx_count > 0);

	// Do-while for the case of non-blocking (timeout == -1)
	// so that we do at least one iteration

	do {
		if (_check_epoll_entry(first, events, number_of_events, event_count)) {
			++event_count;
		}

		if (event_count < number_of_events && instance->tssx_count > 1) {
			VECTOR_FOR_EACH(&instance->entries, iterator) {
				EpollEntry *entry = iterator_get(&iterator);
				if (_check_epoll_entry(entry, events, number_of_events, event_count)) {
					if (++event_count == number_of_events) break;
				}
			}
		}

		shared_event_count_value = atomic_load(shared_event_count);

		// Don't touch if there was an error in the normal thread
		if (shared_event_count_value == ERROR) return ERROR;
		if (shared_event_count_value > 0) break;
		if (event_count > 0) {
			_kill_normal_thread(normal_thread);
			break;
		}

	} while (!_poll_timeout_elapsed(start, timeout));

	// Add whatever we have (zero if the
	// timeout elapsed or normal thread had activity)
	atomic_fetch_add(shared_event_count, event_count);

	return event_count;
}

int _tssx_epoll_wait_for_single_entry(EpollEntry *entry,
																			struct epoll_event *events,
																			size_t number_of_events,
																			int timeout) {
	size_t start = current_milliseconds();

	do {
		if (_check_epoll_entry(entry, events, number_of_events, 0)) break;
	} while (!_poll_timeout_elapsed(start, timeout));

	return SUCCESS;
}

bool _check_epoll_entry(EpollEntry *entry,
												struct epoll_event *events,
												size_t number_of_events,
												size_t event_count) {
	bool activity;
	struct epoll_event *output_event;
	assert(entry != NULL);

	if (event_count == number_of_events) return false;

	// For EPOLLONESHOT behavior
	if (!_is_enabled(entry)) return false;

	output_event = &events[event_count];
	output_event->events = 0;
	activity = false;

	if (_epoll_peer_died(entry)) {
		_notify_of_epoll_hangup(entry, output_event);
		activity = true;
	} else {
		if (_check_epoll_event(entry, output_event, READ)) activity = true;
		if (_check_epoll_event(entry, output_event, WRITE)) activity = true;
	}

	// Copy over the user data
	if (activity) output_event->data = entry->event.data;

	return activity;
}

bool _check_epoll_event(EpollEntry *entry,
												struct epoll_event *output_event,
												Operation operation) {
	if (_epoll_operation_registered(entry, operation)) {
		// This here is a "polymorphic" call to the client/server overrides
		if (_ready_for(entry->connection, operation)) {
			if (_is_level_triggered(entry) || _is_edge_for(entry, operation)) {
				output_event->events |= _epoll_operation_map[operation];
				_set_poll_edge(entry, operation);
				if (_is_oneshot(entry)) _disable_poll_entry(entry);
				return true;
			}
		} else {
			_clear_poll_edge(entry, operation);
		}
	}

	return false;
}

bool _epoll_operation_registered(EpollEntry *entry, size_t operation_index) {
	assert(entry != NULL);
	assert(operation_index == READ || operation_index == WRITE);
	return _epoll_event_registered(entry, _epoll_operation_map[operation_index]);
}

bool _epoll_event_registered(const EpollEntry *entry, int event) {
	assert(entry != NULL);
	assert(event == EPOLLIN || event == EPOLLOUT || event == EPOLLRDHUP);
	return entry->event.events & event;
}

bool _epoll_peer_died(EpollEntry *entry) {
	assert(entry != NULL);
	return connection_peer_died(entry->connection);
}

void _notify_of_epoll_hangup(const EpollEntry *entry,
														 struct epoll_event *output_event) {
	assert(entry != NULL);
	assert(output_event != NULL);

	// This flag is always set, the user's decision is ignored
	output_event->events |= EPOLLHUP;

	// read() will return zero on a dead peer
	output_event->events |= EPOLLIN;

	// EPOLLRDHUP is the actual "peer has died" flag the user will
	// set if he/she wishes to wait for a hangup event, so we must check for it
	if (entry->event.events & EPOLLRDHUP) {
		output_event->events |= EPOLLRDHUP;
	}
}

void _invalid_argument_exception() {
	errno = EINVAL;
	print_error("Descriptor was not registered with epoll instance\n");
}

void _destroy_epoll_lock() {
	if (pthread_mutex_destroy(&_epoll_lock) != SUCCESS) {
		print_error("Error destroying mutex\n");
	}
}

bool _is_edge_triggered(const EpollEntry *entry) {
	assert(entry != NULL);
	return entry->event.events & EPOLLET;
}

bool _is_level_triggered(const EpollEntry *entry) {
	return !_is_edge_triggered(entry);
}

bool _is_oneshot(const EpollEntry *entry) {
	assert(entry != NULL);
	return entry->event.events & EPOLLONESHOT;
}

bool _is_edge_for(const EpollEntry *entry, Operation operation) {
	assert(entry != NULL);
	return !(entry->flags & (operation == READ) ? READ_EDGE : WRITE_EDGE);
}

bool _is_enabled(const EpollEntry *entry) {
	assert(entry != NULL);
	return entry->flags & ENABLED;
}

void _set_poll_edge(EpollEntry *entry, Operation operation) {
	assert(entry != NULL);
	entry->flags |= (operation == READ) ? READ_EDGE : WRITE_EDGE;
}

void _clear_poll_edge(EpollEntry *entry, Operation operation) {
	assert(entry != NULL);
	entry->flags &= ~((operation == READ) ? READ_EDGE : WRITE_EDGE);
}

void _enable_poll_entry(EpollEntry *entry) {
	assert(entry != NULL);
	entry->flags |= ENABLED;
}

void _disable_poll_entry(EpollEntry *entry) {
	assert(entry != NULL);
	entry->flags &= ~ENABLED;
}

int _validate_epoll_wait_arguments(int epfd, int number_of_events) {
	if (epfd < 0 || epfd >= NUMBER_OF_EPOLL_INSTANCES) {
		print_error("Invalid epoll instance file descriptor\n");
		errno = EINVAL;
		return ERROR;
	}

	if (number_of_events == 0) {
		print_error("epoll event buffer sizes may not be zero\n");
		errno = EINVAL;
		return ERROR;
	}

	return SUCCESS;
}

int _lazy_epoll_setup() {
	if (!_epoll_instances_are_initialized) {
		return _setup_epoll_instances();
	}
	return SUCCESS;
}

bool _lazy_epoll_setup_and_error() {
	if (!_epoll_instances_are_initialized) {
		_setup_epoll_instances();
		errno = EINVAL;
		return true;
	}

	return false;
}
