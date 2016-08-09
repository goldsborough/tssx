#ifndef EPOLL_OVERRIDES_H
#define EPOLL_OVERRIDES_H

#include <signal.h>
#include <stddef.h>
#include <sys/epoll.h>

#include "tssx/common-poll-overrides.h"

/******************** DEFINITIONS ********************/

#define NUMBER_OF_EPOLL_INSTANCES 1024
#define INITIAL_INSTANCE_CAPACITY 8
#define EPOLL_HANGUP EPOLLRDHUP

/******************** REAL FUNCTIONS ********************/

typedef int (*real_epoll_create_t)(int);
typedef int (*real_epoll_create1_t)(int);
typedef int (*real_epoll_ctl_t)(int, int, int, struct epoll_event *);
typedef int (*real_epoll_wait_t)(int, struct epoll_event *, int, int);

// clang-format off
typedef int (*real_epoll_pwait_t)
  (int, struct epoll_event *, int, int, const sigset_t *);
// clang-format on

/******************** STRUCTURES ********************/

typedef int epoll_operation_t;

typedef struct EpollEntry {
	int fd;
	struct poll_event event;
	Connection *connection;
} EpollEntry;

typedef struct EpollInstance {
	struct Vector entries;
	struct EpollEntry first;
	size_t tssx_count;
	size_t normal_count;
} EpollInstance;

typedef struct EpollTask {
	int epfd;
	struct epoll_event *events;
	size_t number_of_events;
	int timeout;
	ready_count_t *ready_count;
	size_t other_ready_count;
} EpollTask;

/******************** REAL FUNCTIONS ********************/

int real_epoll_create(int size);
int real_epoll_create1(int flags);
int real_epoll_ctl(int epfd, int operation, int fd, struct epoll_event *event);
int real_epoll_wait(int epfd,
										struct epoll_event *events,
										int number_of_events,
										int timeout);
int real_epoll_pwait(int epfd,
										 struct epoll_event *events,
										 int number_of_events,
										 int timeout,
										 const sigset_t *sigmask);

/******************** OVERRIDES ********************/

int epoll_create(int size);
int epoll_create1(int flags);

int epoll_ctl(int epfd, int operation, int fd, struct epoll_event *event);

int epoll_wait(int epfd,
							 struct epoll_event *events,
							 int number_of_events,
							 int timeout);
int epoll_pwait(int epfd,
								struct epoll_event *events,
								int number_of_events,
								int timeout,
								const sigset_t *sigmask);

/******************** PRIVATE DEFINITIONS ********************/

#define SUPPORTED_OPERATIONS 3

extern epoll_operation_t _supported_operations[SUPPORTED_OPERATIONS];
extern EpollInstance _epoll_instances[NUMBER_OF_EPOLL_INSTANCES];
extern bool _epoll_instances_are_initialized;

extern pthread_mutex_t _epoll_lock;

/******************** PRIVATE ********************/

void _setup_epoll_instances();

int _epoll_other_tssx_operation(int epfd,
																int operation,
																int fd,
																struct epoll_event *event);

int _epoll_tssx_control_operation(int epfd,
																	int operation,
																	int fd,
																	struct epoll_event *event,
																	Session *session);

int _epoll_add_to_instance(EpollInstance *instance,
													 int fd,
													 struct epoll_event *event,
													 Session *session);

int _epoll_set_first_entry(EpollInstance *instance,
													 int fd,
													 struct epoll_event *event,
													 Session *session);

int _epoll_push_back_entry(EpollInstance *instance,
													 int fd,
													 struct epoll_event *event,
													 Session *session);

int _epoll_update_events(EpollInstance *instance,
												 int fd,
												 const struct epoll_event *new_event);

EpollEntry *_find_epoll_entry(EpollInstance *instance, int fd);

int _epoll_erase_from_instance(EpollInstance *instance, int fd);
int _epoll_erase_first_from_instance(EpollInstance *instance);

int _simple_tssx_epoll_wait(const EpollInstance *instance,
														struct epoll_event *events,
														size_t number_of_events,
														int timeout);

int _concurrent_epoll_wait(int epfd,
													 const EpollInstance *instance,
													 struct epoll_event *events,
													 size_t number_of_events,
													 int timeout);
void _start_other_poll_thread(pthread_t *other_thread, EpollTask *task);
void _other_epoll(EpollTask *task);
void _concurrent_tssx_epoll(EpollInstance *instance,
														struct epoll_event *events,
														size_t number_of_events,
														pthread_t other_thread);

int _tssx_epoll_wait_for_single_entry(const EpollEntry *entry,
																			struct epoll_event *events,
																			int timeout);

bool _check_epoll_entry(const EpollEntry *entry,
												struct epoll_event *events,
												size_t number_of_events,
												size_t event_count);
bool _epoll_ready_for_operation(EpollEntry *entry,
																size_t operation_index,
																struct epoll_event *event);
bool _epoll_event_registered(const EpollEntry *entry, size_t operation_index);
bool _check_for_hangup(EpollEntry *entry, struct epoll_event *event);

Operation _convert_operation(size_t operation_index);
void _invalid_argument_exception();

void _destroy_epoll_lock();

#endif /* EPOLL_OVERRIDES_H */
