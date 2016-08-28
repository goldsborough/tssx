#ifndef TRY_COMMON_H
#define TRY_COMMON_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/************************ DEFINITIONS ************************/

#define ERROR -1
#define SUCCESS 0
#define TIMEOUT 5000 // milliseconds

#define SOCKET_PATH "/tmp/try-socket"
#define MESSAGE_SIZE 4096
#define DEFAULT_METHOD SELECT
#define BACKLOG 10
#define TIMEOUT_SECONDS 10

typedef enum Method { SELECT, POLL, EPOLL } Method;

struct timeval;

/************************ INTERFACE ************************/

void throw(const char *message);
void die(const char *message);
void print_error(const char *message);
void setup_timeout(struct timeval *timeout);
void set_cloexec_flag(int socket_fd);
void set_nonblocking(int socket_fd);

#endif /* TRY_COMMON_H */
