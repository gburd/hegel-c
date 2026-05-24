#include "runner.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Maximum time to wait for the server to exit after SIGTERM (in microseconds). */
#define SIGTERM_WAIT_US          2000000  /* 2 seconds */
#define SIGTERM_POLL_US            10000  /* 10 ms */

hegel_session *hegel_session_new(void)
{
    hegel_session *s = calloc(1, sizeof(hegel_session));
    if (!s)
        return NULL;

    s->conn = NULL;
    s->server_pid = -1;

    /* Determine the server command */
    const char *server_cmd = getenv("HEGEL_SERVER_COMMAND");
    if (!server_cmd || server_cmd[0] == '\0')
        server_cmd = "hegel";

    /* Create pipes for stdin/stdout communication with the server.
     * pipe_to_server:   parent writes -> child reads (child's stdin)
     * pipe_from_server: child writes -> parent reads (child's stdout)
     */
    int pipe_to_server[2];   /* [0]=read end, [1]=write end */
    int pipe_from_server[2]; /* [0]=read end, [1]=write end */

    if (pipe(pipe_to_server) < 0) {
        free(s);
        return NULL;
    }
    if (pipe(pipe_from_server) < 0) {
        close(pipe_to_server[0]);
        close(pipe_to_server[1]);
        free(s);
        return NULL;
    }

    /* Fork and exec the server */
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_to_server[0]);
        close(pipe_to_server[1]);
        close(pipe_from_server[0]);
        close(pipe_from_server[1]);
        free(s);
        return NULL;
    }

    if (pid == 0) {
        /* Child process: set up pipes as stdin/stdout */
        close(pipe_to_server[1]);   /* close write end of to-server pipe */
        close(pipe_from_server[0]); /* close read end of from-server pipe */

        /* Redirect stdin from pipe_to_server[0] */
        if (dup2(pipe_to_server[0], STDIN_FILENO) < 0)
            _exit(127);
        close(pipe_to_server[0]);

        /* Redirect stdout to pipe_from_server[1] */
        if (dup2(pipe_from_server[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(pipe_from_server[1]);

        /* Exec the hegel server (no positional args) */
        execlp(server_cmd, server_cmd, (char *)NULL);
        /* If exec fails, exit with error */
        _exit(127);
    }

    /* Parent process */
    s->server_pid = pid;
    close(pipe_to_server[0]);   /* close read end (child uses it) */
    close(pipe_from_server[1]); /* close write end (child uses it) */

    /* Parent reads from pipe_from_server[0], writes to pipe_to_server[1] */
    int read_fd = pipe_from_server[0];
    int write_fd = pipe_to_server[1];

    /* Create connection object */
    s->conn = hegel_connection_new(read_fd, write_fd);
    if (!s->conn) {
        close(read_fd);
        close(write_fd);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        free(s);
        return NULL;
    }

    /* Perform protocol handshake */
    int rc = hegel_connection_handshake(s->conn);
    if (rc != HEGEL_OK) {
        fprintf(stderr, "hegel: handshake failed (error %d)\n", rc);
        hegel_connection_free(s->conn);
        s->conn = NULL;
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        free(s);
        return NULL;
    }

    return s;
}

void hegel_session_free(hegel_session *s)
{
    if (!s)
        return;

    /* Close connection (this closes the pipe fds and frees streams) */
    if (s->conn) {
        hegel_connection_free(s->conn);
        s->conn = NULL;
    }

    /* Terminate the server subprocess */
    if (s->server_pid > 0) {
        kill(s->server_pid, SIGTERM);

        /* Wait for graceful shutdown */
        int elapsed = 0;
        int status;
        pid_t result;
        while (elapsed < SIGTERM_WAIT_US) {
            result = waitpid(s->server_pid, &status, WNOHANG);
            if (result != 0)
                goto reaped;
            usleep(SIGTERM_POLL_US);
            elapsed += SIGTERM_POLL_US;
        }

        /* Force kill if still running */
        kill(s->server_pid, SIGKILL);
        waitpid(s->server_pid, &status, 0);
    reaped:
        s->server_pid = -1;
    }

    free(s);
}
