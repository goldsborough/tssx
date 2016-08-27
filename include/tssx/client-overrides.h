#ifndef CLIENT_OVERRIDES_H
#define CLIENT_OVERRIDES_H

#include "tssx/definitions.h"

/******************** FORWARD DECLARATIONS ********************/

int _setup_tssx(int fd);
int _read_segment_id_from_server(int fd);
int _synchronize_with_server(int fd);

#endif /* CLIENT_OVERRIDES_H */
