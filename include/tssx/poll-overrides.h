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

/******************** HELPERS ********************/

extern const short _operation_map[2];

int _partition(struct Vector *tssx_fds,
							 struct Vector *normal_fds,
							 struct pollfd fds[],
							 nfds_t number);

PollEntry _create_entry(struct pollfd *poll_pointer);

int _start_normal_poll_thread(pthread_t *poll_thread, PollTask *task);

int _simple_tssx_poll(struct Vector *tssx_fds, int timeout);

int _concurrent_poll(struct Vector *tssx_fds,
										 struct Vector *normal_fds,
										 int timeout);
void _normal_poll(PollTask *task);
void _concurrent_tssx_poll(PollTask *task, pthread_t normal_thread);

bool _check_ready(PollEntry *entry, Operation operation);
bool _waiting_for(PollEntry *entry, Operation operation);
bool _tell_that_ready_for(PollEntry *entry, Operation operation);
void _check_for_poll_hangup(PollEntry *entry);

void _cleanup(struct Vector *tssx_fds, struct Vector *normal_fds);

#endif /* POLL_OVERRIDES_H */
