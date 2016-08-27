#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>


/******************** DEFINITIONS ********************/

#define SESSION_INITIALIZER \
	{ NULL }

struct Connection;

/******************** STRUCTURES ********************/

/**
 * This structure is there in case future changes require
 * a modification to the session (an element in the bridge)
 * as it was necessary before.
 */
// clang-format off
typedef struct Session {
	struct Connection* connection;
} Session;
// clang-format on

/******************** INTERFACE ********************/

void session_setup(Session* session);

bool session_has_connection(const Session* session);
void session_invalidate(Session* session);

#endif /* SESSION_H */
