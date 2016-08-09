#define _GNU_SOURCE

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "tssx/connection.h"
#include "tssx/bridge.h"
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

	if ((epfd = epoll_create1(flags)) == ERROR) {
		return ERROR;
	}

	if (!_epoll_instances_are_initialized) {
		if (_setup_epoll_instances() == ERROR) {
			return ERROR;
		}
	}

	assert(!has_epoll_instance_associated(epfd));

	return epfd;
}

int epoll_ctl(int epfd, int operation, int fd, struct epoll_event *event) {
	Session *session;
	assert(_epoll_instances_are_initialized);

	// epoll_create() cannot yet have been called, thus epfd will be invalid
	if (!_epoll_instances_are_initialized) {
		errno = EINVAL;
		return ERROR;
	}

	session = bridge_lookup(&bridge, fd);
	if (session_has_connection(session)) {
		return _epoll_tssx_operation(epfd, operation, fd, event, session);
	} else {
		return _epoll_other_operation(epfd, operation, fd, event);
	}
}

int epoll_wait(int epfd,
							 struct epoll_event *events,
							 int number_of_events,
							 int timeout) {
	EpollInstance *instance;
	assert(_epoll_instances_are_initialized);

	// epoll_create() cannot yet have been called, thus epfd will be invalid
	if (number_of_events == 0 || !_epoll_instances_are_initialized) {
		errno = EINVAL;
		return ERROR;
	}

	instance = _epoll_instances[epfd];

	if (instance->normal_count == 0) {
		return _simple_tssx_epoll_wait(instance, events, number_of_events, timeout);
	} else if (instance->tssx_count == 0) {
		return real_epoll_wait(epfd, events, number_of_events, timeout);
	} else {
		return _concurrent_tssx_epoll_wait(epfd, events, number_of_events, timeout);
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
		return epoll_wait(efpd, events, number_of_events, timeout);
	}

	if (pthread_mutex_lock(&_epoll_lock) != SUCCESS) {
		return ERROR;
	}

	if (pthread_sigmask(SIG_SETMASK, sigmask, &original_mask) != SUCCESS) {
		return ERROR;
	}

	event_count = epoll_wait(efpd, events, number_of_events, timeout);

	if (pthread_sigmask(SIG_SETMASK, sigmask, &original_mask) != SUCCESS) {
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
	assert(epfd > 0);
	assert(epfd < NUMBER_OF_EPOLL_INSTANCES);
	return _epoll_instances[epfd].tssx_count +
				 _epoll_instances[epfd].normal_count;
}

int close_epoll_instance(int epfd) {
	assert(epfd > 0);
	assert(epfd < NUMBER_OF_EPOLL_INSTANCES);

	if (tssx_count > 1) {
		if (vector_destroy(&_epoll_instances[epfd].entries) == VECTOR_ERROR) {
			return ERROR;
		}
	}

	tssx_count = 0;
	normal_count = 0;

	return SUCCESS;
}

/******************** PRIVATE DEFINITIONS ********************/

// clang-format off
epoll_operation_t _supported_operations[SUPPORTED_OPERATIONS] = {
  EPOLLIN,
  EPOLLOUT,
  EPOLL_HANGUP // EPOLLRDHUP
};
// clang-format on

EpollInstance _epoll_instances[NUMBER_OF_EPOLL_INSTANCES];
bool _epoll_instances_are_initialized = false;

pthread_mutex_t _epoll_lock = PTHREAD_MUTEX_INITIALIZER;

/******************** PRIVATE ********************/

int _setup_epoll_instances() {
	assert(!_epoll_instances_are_initialized);

	for (size_t instance = 0; instance <= NUMBER_OF_EPOLL_INSTANCES; ++instance) {
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

int _epoll_other_tssx_operation(int epfd,
																int operation,
																int fd,
																struct epoll_event *event) {
	if (operation == EPOLL_CTL_ADD) {
		_epoll_instances[epfd].other_count++;
	} else if (operation == EPOLL_CTL_ADD) {
		_epoll_instances[epfd].other_count--;
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
     return _epoll_update_events(instance, fd, event);
     break;
		case EPOLL_CTL_DEL:
			return _epoll_erase_from_instance(instance, fd, event);
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

	++instance->tssx_count;

	return SUCCESS;
}

int _epoll_push_back_entry(EpollInstance *instance,
													 int fd,
													 struct epoll_event *event,
													 Session *session) {
	EpollEntry entry = {fd, *event, session->connection};

	if (instance->tssx_count == 1) {
		// clang-format off
		int code = vector_setup(
        &instance->events,
        INITIAL_INSTANCE_CAPACITY,
        sizeof(PollEntry)
    );
		// clang-format on


		if (code == VECTOR_ERROR) return ERROR;
	}

	if (vector_push_back(&instance->events, &entry) == VECTOR_ERROR) {
		return ERROR;
	}

	++instance->tssx_count;

	return SUCCESS;
}

int _epoll_update_events(EpollInstance *instance,
												 int fd,
												 const struct epoll_event *new_event) {
	assert(instance->tssx_count > 0);

	if (fd == instance->first.fd) {
		instance->first.event = *new_event;
	} else {
		EpollEntry *entry = _find_entry(instance, fd);

		if (entry == NULL) {
			_invalid_argument_exception();
			return ERROR;
		} else {
			entry->event = *new_event;
		}
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
			if (iterator_erase(instance->entries, &iterator) == VECTOR_ERROR) {
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

		if (vector_pop_back(&instance->entries) == VECTOR_ERROR) {
			return ERROR;
		}

		instance->first = *entry;
	}

	instance->tssx_count--;

	return SUCCESS;
}

int _simple_tssx_epoll_wait(const EpollInstance *instance,
														struct epoll_event *events,
														size_t number_of_events,
														int timeout) {
	size_t event_count = 0;
	size_t start = current_milliseconds();
	PollEntry *first = &instance->first;

	assert(instance->tssx_count > 0);

	if (instance->tssx_count == 1) {
		return _tssx_epoll_wait_for_single_entry(instance->first, events, timeout);
	}

	do {
		if (_check_epoll_entry(&first, events, number_of_events, event_count)) {
			++event_count;
		}

		VECTOR_FOR_EACH(&instance->entries, iterator) {
			EpollEntry *entry = iterator_get(&iterator);
			if (_check_epoll_entry(&entry, events, number_of_events, event_count)) {
				++event_count;
			}
		}
	} while (event_count == 0 && !_poll_timeout_elapsed(start, timeout));

	return event_count;
}

int _concurrent_epoll_wait(int epfd,
													 const EpollInstance *instance,
													 struct epoll_event *events,
													 size_t number_of_events,
													 int timeout) {
	struct sigaction old_action;
	pthread_t other_thread;
	ready_count_t ready_count = ATOMIC_VAR_INIT(0);
	struct epoll_event *tssx_events;
	size_t tssx_ready_count;

	// clang-format off
  EpollTask other_task = {
    epfd, events, number_of_events, timeout, &ready_count, 0
  };
	// clang-format on

	tssx_events = calloc(number_of_events, sizeof(struct epoll_event);

	if (_install_poll_signal_handler(&old_action) == ERROR) {
		return ERROR;
	}

	if (_start_other_epoll_thread(&other_thread, &other_task) == ERROR) {
		return ERROR;
	}

 tssx_ready_count = _concurrent_tssx_epoll_wait(
      instance,
      tssx_events,
      number_of_events,
      other_thread
  );

  if (pthread_join(other_thread, NULL) == ERROR) {
		return ERROR;
  }

  for (size_t index = 0; index < tssx_ready_count; ++index) {
		events[other_ready_count + index] = tssx_events[index];
	}

  free(tssx_events);

  _restore_old_signal_action(&old_action);

  assert(tssx_ready_count + other_ready_count == atomic_load(&ready_count));

  return atomic_load(&ready_count);
}

void _start_other_poll_thread(pthread_t *other_thread, EpollTask *task) {
	thread_function_t function = (thread_function_t)_other_epoll;

	if (pthread_create(other_thread, NULL, function, task) != SUCCESS) {
		print_error("Error creating epoll thread");
		return ERROR;
	}

	return SUCCESS;
}

void _other_epoll(EpollTask *task) {
	int local_ready_count;
	assert(task != NULL);

	// clang-format off
	local_ready_count = real_epoll_wait(
      task->epfd,
      task->events,
      task->number_of_events,
      task->timeout
  );
	// clang-format on

	// Don't touch anything else if there was an error in the main thread
	if (_there_was_an_error(task->ready_count)) return;

	// Check if there was an error in real_epoll, but not EINTR, which
	// would be the signal received when one or more TSSX fd was ready
	if (local_ready_count == ERROR) {
		if (errno != EINTR) {
			atomic_store(task->ready_count, ERROR);
		}
		return;
	}

	// Either there was a timeout (+= 0), or some FDs are ready
	atomic_fetch_add(task->ready_count, local_ready_count);
	task->other_ready_count = local_ready_count;
}

void _concurrent_tssx_epoll(EpollInstance *instance,
														struct epoll_event *events,
														size_t number_of_events,
														pthread_t other_thread) {
	size_t global_ready_count;
	size_t start = current_milliseconds();
	size_t local_ready_count = 0;
	PollEntry *first = &instance->first;

	assert(task != NULL);
	assert(instance->tssx_count > 0);

	// Do-while for the case of non-blocking (timeout == -1)
	// so that we do at least one iteration

	do {
		if (_check_epoll_entry(&first, events, number_of_events, event_count)) {
			++local_ready_count;
		}

		VECTOR_FOR_EACH(&instance->entries, iterator) {
			EpollEntry *entry = iterator_get(&iterator);
			if (_check_epoll_entry(&entry, events, number_of_events, event_count)) {
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

	} while (!_poll_timeout_elapsed(start, timeout));

	// Add whatever we have (zero if the
	// timeout elapsed or other thread had activity)
	atomic_fetch_add(task->ready_count, local_ready_count);
}

int _tssx_epoll_wait_for_single_entry(const EpollEntry *entry,
																			struct epoll_event *events,
																			int timeout) {
	size_t start = current_milliseconds();

	do {
		if (_check_epoll_entry(entry, events, number_of_events, 0)) break;
	} while (!_poll_timeout_elapsed(start, timeout));

	return SUCCESS;
}

bool _check_epoll_entry(const EpollEntry *entry,
												struct epoll_event *events,
												size_t number_of_events,
												size_t event_count) {
	int code;
	bool activity;
	assert(entry != NULL);

	if (event_count == number_of_events) return false;

	activity = false;
	for (size_t index = 0; index < SUPPORTED_OPERATIONS; ++index) {
		struct epoll_event *event = events[event_count];
		event->events = 0;
		if (!_epoll_event_registered(entry, index)) continue;

		if (_supported_operations[index] == EPOLL_HANGUP) {
			if (_check_for_hangup(entry, event)) {
				activity = true;
			}
		} else if (_epoll_ready_for_operation(entry, index, event)) {
			activity = true;
		}
	}

	// Copy over the user data
	if (activity) event->data = entry->event.data;

	return false;
}

bool _epoll_ready_for_operation(EpollEntry *entry,
																size_t operation_index,
																struct epoll_event *event) {
	Operation operation;
	assert(event != NULL);

	operation = _convert_operation(operation_index);
	// This here is a "polymorphic" call to the client/server overrides
	if (_ready_for(entry->connection, operation)) {
		event->events |= _supported_operations[operation_index];
		return true;
	}

	return false;
}

bool _check_for_hangup(EpollEntry *entry, struct epoll_event *event) {
	assert(entry != NULL);
	assert(entry->connection != NULL);
	assert(event != NULL);

	if (connection_peer_died(entry->connection)) {
		event->events |= EPOLL_HANGUP;
		return true;
	}

	return false;
}

Operation _convert_operation(size_t operation_index) {
	assert(operation_index < SUPPORTED_OPERATIONS);
	if (_supported_operations[operation_index] == EPOLLIN) return READ;
	if (_supported_operations[operation_index] == EPOLLOUT) return WRITE;

	assert(false);
}

bool _epoll_event_registered(const EpollEntry *entry, size_t operation_index) {
	assert(entry != NULL);
	assert(operation_index < SUPPORTED_OPERATIONS);
	return entry->event.events & _supported_operations[operation_index]);
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
