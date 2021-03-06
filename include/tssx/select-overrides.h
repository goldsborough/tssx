#ifndef SELECT_OVERRIDES_H
#define SELECT_OVERRIDES_H

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/select.h>

#include "tssx/definitions.h"

/******************** DEFINITIONS ********************/

// clang-format off
typedef int (*real_select_t)(
		int, fd_set *, fd_set *, fd_set *, struct timeval *
);

typedef int (*real_pselect_t)(
    int, fd_set *, fd_set *, fd_set *,
     const struct timespec *, const sigset_t *
);
// clang-format on

struct pollfd;
struct timespec;

typedef struct DescriptorSets {
	fd_set *readfds;
	fd_set *writefds;
	fd_set *errorfds;
} DescriptorSets;

/******************** REAL FUNCTIONS ********************/

int real_select(int nfds,
								fd_set *readfds,
								fd_set *writefds,
								fd_set *errorfds,
								struct timeval *timeout);

int real_pselect(int nfds,
								 fd_set *readfds,
								 fd_set *writefds,
								 fd_set *errorfds,
								 const struct timespec *timeout,
								 const sigset_t *sigmask);

/******************** OVERRIDES ********************/

int select(int nfds,
					 fd_set *readfds,
					 fd_set *writefds,
					 fd_set *errorfds,
					 struct timeval *timeout);

int pselect(int nfds,
						fd_set *readfds,
						fd_set *writefds,
						fd_set *errorfds,
						const struct timespec *timeout,
						const sigset_t *sigmask);

/******************** PRIVATE DEFINITIONS ********************/

extern pthread_mutex_t _select_lock;
extern bool _select_is_initialized;

/******************** HELPERS ********************/

int _forward_to_poll(size_t highest_fd,
										 DescriptorSets *sets,
										 size_t population_count,
										 struct timeval *timeout);

struct pollfd *_setup_poll_entries(size_t population_count,
																	 const DescriptorSets *sets,
																	 size_t highest_fd);
void _fill_poll_entries(struct pollfd *poll_entries,
												const DescriptorSets *sets,
												size_t highest_fd);

int _read_poll_entries(DescriptorSets *sets,
											 struct pollfd *poll_entries,
											 size_t population_count);

int _select_on_tssx_only(DescriptorSets *sets,
												 size_t tssx_count,
												 size_t lowest_fd,
												 size_t highest_fd,
												 struct timeval *timeout);

int _select_on_tssx_only_fast_path(DescriptorSets *sets,
																	 size_t fd,
																	 struct timeval *timeout);

void _count_tssx_sockets(size_t highest_fd,
												 const DescriptorSets *sets,
												 size_t *lowest_fd,
												 size_t *normal_count,
												 size_t *tssx_count);

bool _is_in_any_set(int fd, const DescriptorSets *sets);

void _copy_all_sets(DescriptorSets *destination, const DescriptorSets *source);
void _copy_set(fd_set *destination, const fd_set *source);

void _clear_all_sets(DescriptorSets *sets);
bool _fd_is_set(int fd, const fd_set *set);

bool _check_select_events(int fd, Session *session, DescriptorSets *sets);

bool _select_peer_died(int fd, Session *session, DescriptorSets *sets);

bool _waiting_and_ready_for_select(int fd,
																	 Session *session,
																	 const DescriptorSets *sets,
																	 Operation operation);

bool _ready_for_select(int fd,
											 Session *session,
											 fd_set *set,
											 Operation operation);

bool _check_poll_event_occurred(const struct pollfd *entry,
																DescriptorSets *sets,
																int event);

fd_set *_fd_set_for_operation(const DescriptorSets *sets, Operation operation);
fd_set *_fd_set_for_poll_event(const DescriptorSets *sets, int poll_event);

fd_set *_fd_set_for_operation(const DescriptorSets *sets, Operation operation);
fd_set *_fd_set_for_poll_event(const DescriptorSets *sets, int poll_event);

bool _select_timeout_elapsed(size_t start, int timeout);

void _clear_set(fd_set *set);

int _lazy_select_setup();
int _setup_select();
void _destroy_select_lock();

#endif /* SELECT_OVERRIDES_H */
