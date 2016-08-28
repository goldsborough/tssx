#ifndef COMMON_POLL_OVERRIDES_H
#define COMMON_POLL_OVERRIDES_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "tssx/definitions.h"

/******************** DEFINITIONS ********************/

#define POLL_SIGNAL SIGUSR1
#define BLOCK_FOREVER -1
#define DONT_BLOCK 0

typedef void *(*thread_function_t)(void *);
typedef atomic_int_fast16_t event_count_t;

struct Connection;
struct sigaction;
struct pthread_mutex_t;
struct sigset_t;

/******************** HELPERS ********************/

bool _there_was_an_error(event_count_t *event_count);
bool _poll_timeout_elapsed(size_t start, int timeout);

int _install_poll_signal_handler(struct sigaction *old_action);
int _restore_old_signal_action(struct sigaction *old_action);
void _poll_signal_handler(int signal_number);
void _kill_normal_thread(pthread_t normal_thread);

int _set_poll_mask(pthread_mutex_t* lock, const sigset_t *sigmask, sigset_t *original_mask);
int _restore_poll_mask(pthread_mutex_t* lock, const sigset_t *original_mask);

/******************** "POLYMORPHIC" FUNCTIONS ********************/

bool _ready_for(struct Connection *entry, Operation operation);

#endif /* COMMON_POLL_OVERRIDES_H */
