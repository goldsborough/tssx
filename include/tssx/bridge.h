#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdbool.h>

#include "tssx/definitions.h"
#include "tssx/session-table.h"

/******************** DEFINITIONS ********************/

#define BRIDGE_INITIALIZER \
	{ false }

struct Session;

/******************** STRUCTURES ********************/

// clang-format off
typedef struct Bridge {
  bool is_initialized;
  SessionTable session_table;
} Bridge;
// clang-format on

extern Bridge bridge;

/******************** INTERFACE ********************/

int bridge_setup(Bridge* bridge);
int bridge_destroy(Bridge* bridge);

bool bridge_is_initialized(const Bridge* bridge);

int bridge_add_user(Bridge* bridge);

int bridge_insert(Bridge* bridge, int fd, struct Session* session);
int bridge_erase(Bridge* bridge, int fd);

struct Session* bridge_lookup(Bridge* bridge, int fd);
bool bridge_has_connection(Bridge* bridge, int fd);

/******************** PRIVATE ********************/

extern signal_handler_t old_sigint_handler;
extern signal_handler_t old_sigterm_handler;
extern signal_handler_t old_sigabrt_handler;

int _lazy_bridge_setup(Bridge* bridge);

void _setup_exit_handling();
void _setup_signal_handler(int signal_number);

void _bridge_signal_handler();
void _bridge_signal_handler_for(int signal_number,
																signal_handler_t old_handler);
void _bridge_exit_handler();

#endif /* BRIDGE_H */
