#ifndef HEGEL_RUNNER_H
#define HEGEL_RUNNER_H

#include "hegel/types.h"
#include "hegel/protocol.h"

#include <sys/types.h>

/*
 * Session state: manages the server subprocess and connection.
 */
struct hegel_session {
    hegel_connection *conn;
    pid_t server_pid;
};

/*
 * Create a new session: spawn the hegel server subprocess, connect, handshake.
 * Returns NULL on failure.
 */
hegel_session *hegel_session_new(void);

/*
 * Free a session: close connection, kill server.
 */
void hegel_session_free(hegel_session *s);

#endif /* HEGEL_RUNNER_H */
