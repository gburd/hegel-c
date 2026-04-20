#include "runner.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* Maximum time to wait for the server socket to appear (in microseconds). */
#define SOCKET_WAIT_TIMEOUT_US  10000000  /* 10 seconds */
#define SOCKET_POLL_INTERVAL_US    10000  /* 10 ms */

/* Maximum time to wait for the server to exit after SIGTERM (in microseconds). */
#define SIGTERM_WAIT_US          2000000  /* 2 seconds */
#define SIGTERM_POLL_US            10000  /* 10 ms */

/*
 * Wait for a Unix socket file to appear on disk.
 * Returns 0 on success, -1 on timeout.
 */
static int wait_for_socket(const char *path)
{
    int elapsed = 0;
    struct stat st;

    while (elapsed < SOCKET_WAIT_TIMEOUT_US) {
        if (stat(path, &st) == 0 && (st.st_mode & S_IFSOCK))
            return 0;
        usleep(SOCKET_POLL_INTERVAL_US);
        elapsed += SOCKET_POLL_INTERVAL_US;
    }
    return -1;
}

/*
 * Connect to a Unix domain socket at the given path.
 * Returns the socket fd, or -1 on failure.
 */
static int connect_unix(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (strlen(path) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

hegel_session *hegel_session_new(void)
{
    hegel_session *s = calloc(1, sizeof(hegel_session));
    if (!s)
        return NULL;

    s->conn = NULL;
    s->server_pid = -1;
    s->socket_path = NULL;
    s->temp_dir = NULL;

    /* Create a temporary directory for the socket */
    char template[] = "/tmp/hegel-XXXXXX";
    char *tmpdir = mkdtemp(template);
    if (!tmpdir) {
        free(s);
        return NULL;
    }
    s->temp_dir = strdup(tmpdir);
    if (!s->temp_dir) {
        rmdir(tmpdir);
        free(s);
        return NULL;
    }

    /* Build socket path */
    size_t path_len = strlen(s->temp_dir) + strlen("/socket") + 1;
    s->socket_path = malloc(path_len);
    if (!s->socket_path) {
        rmdir(s->temp_dir);
        free(s->temp_dir);
        free(s);
        return NULL;
    }
    snprintf(s->socket_path, path_len, "%s/socket", s->temp_dir);

    /* Determine the server command */
    const char *server_cmd = getenv("HEGEL_SERVER_COMMAND");
    if (!server_cmd || server_cmd[0] == '\0')
        server_cmd = "hegel";

    /* Fork and exec the server */
    pid_t pid = fork();
    if (pid < 0) {
        /* Fork failed */
        free(s->socket_path);
        rmdir(s->temp_dir);
        free(s->temp_dir);
        free(s);
        return NULL;
    }

    if (pid == 0) {
        /* Child process: exec the hegel server */
        execlp(server_cmd, server_cmd, s->socket_path, (char *)NULL);
        /* If exec fails, exit with error */
        _exit(127);
    }

    /* Parent process */
    s->server_pid = pid;

    /* Wait for the socket file to appear */
    if (wait_for_socket(s->socket_path) < 0) {
        fprintf(stderr, "hegel: timed out waiting for server socket\n");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        unlink(s->socket_path);
        rmdir(s->temp_dir);
        free(s->socket_path);
        free(s->temp_dir);
        free(s);
        return NULL;
    }

    /* Connect to the server */
    int fd = connect_unix(s->socket_path);
    if (fd < 0) {
        fprintf(stderr, "hegel: failed to connect to server socket\n");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        unlink(s->socket_path);
        rmdir(s->temp_dir);
        free(s->socket_path);
        free(s->temp_dir);
        free(s);
        return NULL;
    }

    /* Create connection object */
    s->conn = hegel_connection_new(fd);
    if (!s->conn) {
        close(fd);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        unlink(s->socket_path);
        rmdir(s->temp_dir);
        free(s->socket_path);
        free(s->temp_dir);
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
        unlink(s->socket_path);
        rmdir(s->temp_dir);
        free(s->socket_path);
        free(s->temp_dir);
        free(s);
        return NULL;
    }

    return s;
}

void hegel_session_free(hegel_session *s)
{
    if (!s)
        return;

    /* Close connection (this closes the socket fd and frees streams) */
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

    /* Clean up socket file and temp directory */
    if (s->socket_path) {
        unlink(s->socket_path);
        free(s->socket_path);
        s->socket_path = NULL;
    }
    if (s->temp_dir) {
        rmdir(s->temp_dir);
        free(s->temp_dir);
        s->temp_dir = NULL;
    }

    free(s);
}
