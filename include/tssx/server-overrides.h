#ifndef SERVER_OVERRIDES_H
#define SERVER_OVERRIDES_H

/******************** DEFINITIONS ********************/

struct Session;

/******************** FORWARD DECLARATIONS ********************/

int _setup_tssx(int client_socket);
int _send_segment_id_to_client(int client_socket, struct Session* session);
int _synchronize_with_client(int client_fd);

#endif /* SERVER_OVERRIDES_H */
