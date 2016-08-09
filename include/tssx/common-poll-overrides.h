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

typedef atomic_int_fast16_t ready_count_t;

/******************** HELPERS ********************/

bool _there_was_an_error(ready_count_t *ready_count);
bool _poll_timeout_elapsed(size_t start, int timeout);

int _install_poll_signal_handler(struct sigaction *old_action);
int _restore_old_signal_action(struct sigaction *old_action);
void _poll_signal_handler(int signal_number);
void _kill_other_thread(pthread_t other_thread);

/******************** "POLYMORPHIC" FUNCTIONS ********************/

bool _ready_for(struct Connection *entry, Operation operation);

#endif /* COMMON_POLL_OVERRIDES_H */
