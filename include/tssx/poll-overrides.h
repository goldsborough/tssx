#ifndef POLL_OVERRIDES_H
#define POLL_OVERRIDES_H

#include <poll.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "tssx/common-poll-overrides.h"
#include "tssx/definitions.h"

/******************** DEFINITIONS ********************/

typedef int (*real_poll_t)(struct pollfd[], nfds_t, int);

struct Connection;
struct Vector;
struct sigaction;

typedef struct PollEntry {
	struct Connection *connection;
	struct pollfd *poll_pointer;
} PollEntry;

typedef struct PollTask {
	struct Vector *fds;
	int timeout;
	event_count_t *event_count;
} PollTask;

/******************** REAL FUNCTIONS ********************/

int real_poll(struct pollfd fds[], nfds_t nfds, int timeout);

/******************** OVERRIDES ********************/

int poll(struct pollfd fds[], nfds_t number, int timeout);

/******************** PRIVATE DEFINITIONS ********************/

extern const short _operation_map[2];

extern pthread_mutex_t _poll_lock;
extern pthread_cond_t _poll_condition;
extern bool _normal_thread_ready;
extern bool _poll_is_initialized;

/******************** HELPERS ********************/

int _partition(struct Vector *tssx_fds,
							 struct Vector *normal_fds,
							 struct pollfd fds[],
							 nfds_t number);
void _join_poll_partition(struct pollfd fds[],
													nfds_t number_of_fds,
													struct Vector *normal_fds);

PollEntry _create_entry(struct pollfd *poll_pointer);

int _start_normal_poll_thread(pthread_t *poll_thread, PollTask *task);

int _simple_tssx_poll(struct Vector *tssx_fds, int timeout);

int _concurrent_poll(struct Vector *tssx_fds,
										 struct Vector *normal_fds,
										 int timeout);
void _normal_poll(PollTask *task);
void _concurrent_tssx_poll(PollTask *task, pthread_t normal_thread);

bool _check_poll_events(PollEntry *entry);
bool _check_ready(PollEntry *entry, Operation operation);
bool _waiting_for(PollEntry *entry, Operation operation);
bool _tell_that_ready_for(PollEntry *entry, Operation operation);
bool _entry_peer_died(PollEntry *entry);

void _cleanup(struct Vector *tssx_fds, struct Vector *normal_fds);

int _lazy_poll_setup();
int _setup_poll();
void _destroy_poll_lock_and_condvar();

int _signal_tssx_thread();
int _wait_for_normal_thread();

#endif /* POLL_OVERRIDES_H */
