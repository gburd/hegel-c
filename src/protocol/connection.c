#include "connection.h"
#include "stream.h"
#include "packet.h"
#include "cbor_helpers.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Handshake constants.
 */
static const uint8_t HANDSHAKE_REQUEST[] = "hegel_handshake_start";
static const size_t HANDSHAKE_REQUEST_LEN = sizeof(HANDSHAKE_REQUEST) - 1; /* exclude NUL */
static const char HANDSHAKE_PREFIX[] = "Hegel/";
static const size_t HANDSHAKE_PREFIX_LEN = sizeof(HANDSHAKE_PREFIX) - 1;

hegel_connection *hegel_connection_new(int socket_fd)
{
    hegel_connection *conn = calloc(1, sizeof(hegel_connection));
    if (!conn)
        return NULL;

    conn->socket_fd = socket_fd;
    atomic_init(&conn->next_stream_counter, 1);
    atomic_init(&conn->closed, false);
    conn->handshake_done = false;
    conn->last_error = HEGEL_OK;

    if (pthread_mutex_init(&conn->writer_mu, NULL) != 0) {
        free(conn);
        return NULL;
    }
    if (pthread_mutex_init(&conn->reader_mu, NULL) != 0) {
        pthread_mutex_destroy(&conn->writer_mu);
        free(conn);
        return NULL;
    }
    if (pthread_mutex_init(&conn->streams_mu, NULL) != 0) {
        pthread_mutex_destroy(&conn->reader_mu);
        pthread_mutex_destroy(&conn->writer_mu);
        free(conn);
        return NULL;
    }

    memset(conn->streams, 0, sizeof(conn->streams));

    /* Create and register the control stream (stream 0) */
    conn->control_stream = hegel_stream_new(conn, 0);
    if (!conn->control_stream) {
        pthread_mutex_destroy(&conn->streams_mu);
        pthread_mutex_destroy(&conn->reader_mu);
        pthread_mutex_destroy(&conn->writer_mu);
        free(conn);
        return NULL;
    }
    hegel_connection_register_stream(conn, conn->control_stream);

    return conn;
}

void hegel_connection_free(hegel_connection *conn)
{
    if (!conn)
        return;

    atomic_store(&conn->closed, true);

    /* Free all registered streams */
    for (int i = 0; i < HEGEL_MAX_STREAMS; i++) {
        if (conn->streams[i].occupied && conn->streams[i].stream) {
            hegel_stream_free(conn->streams[i].stream);
            conn->streams[i].stream = NULL;
            conn->streams[i].occupied = false;
        }
    }

    pthread_mutex_destroy(&conn->writer_mu);
    pthread_mutex_destroy(&conn->reader_mu);
    pthread_mutex_destroy(&conn->streams_mu);

    if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
        conn->socket_fd = -1;
    }

    free(conn);
}

int hegel_connection_register_stream(hegel_connection *conn, hegel_stream *stream)
{
    pthread_mutex_lock(&conn->streams_mu);
    for (int i = 0; i < HEGEL_MAX_STREAMS; i++) {
        if (!conn->streams[i].occupied) {
            conn->streams[i].stream_id = stream->stream_id;
            conn->streams[i].stream = stream;
            conn->streams[i].occupied = true;
            pthread_mutex_unlock(&conn->streams_mu);
            return HEGEL_OK;
        }
    }
    pthread_mutex_unlock(&conn->streams_mu);
    return HEGEL_ERR_ALLOC;  /* registry full */
}

void hegel_connection_unregister_stream(hegel_connection *conn, uint32_t stream_id)
{
    pthread_mutex_lock(&conn->streams_mu);
    for (int i = 0; i < HEGEL_MAX_STREAMS; i++) {
        if (conn->streams[i].occupied && conn->streams[i].stream_id == stream_id) {
            conn->streams[i].occupied = false;
            conn->streams[i].stream = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&conn->streams_mu);
}

hegel_stream *hegel_connection_find_stream(hegel_connection *conn, uint32_t stream_id)
{
    hegel_stream *result = NULL;
    pthread_mutex_lock(&conn->streams_mu);
    for (int i = 0; i < HEGEL_MAX_STREAMS; i++) {
        if (conn->streams[i].occupied && conn->streams[i].stream_id == stream_id) {
            result = conn->streams[i].stream;
            break;
        }
    }
    pthread_mutex_unlock(&conn->streams_mu);
    return result;
}

int hegel_connection_read_packet(hegel_connection *conn, hegel_packet *pkt)
{
    if (atomic_load(&conn->closed))
        return HEGEL_ERR_CLOSED;

    /* Read 20-byte header */
    uint8_t header[HEGEL_HEADER_SIZE];
    int rc = hegel_read_exact(conn->socket_fd, header, HEGEL_HEADER_SIZE);
    if (rc != HEGEL_OK) {
        atomic_store(&conn->closed, true);
        return rc;
    }

    /* Parse payload length from header bytes 16-19 (big-endian) */
    uint32_t payload_len = ((uint32_t)header[16] << 24) |
                           ((uint32_t)header[17] << 16) |
                           ((uint32_t)header[18] << 8)  |
                           ((uint32_t)header[19]);

    /* Allocate a buffer for header + payload + terminator */
    size_t total = HEGEL_HEADER_SIZE + payload_len + 1;
    uint8_t *buf = malloc(total);
    if (!buf)
        return HEGEL_ERR_ALLOC;

    memcpy(buf, header, HEGEL_HEADER_SIZE);

    /* Read payload + terminator */
    if (payload_len + 1 > 0) {
        rc = hegel_read_exact(conn->socket_fd, buf + HEGEL_HEADER_SIZE, payload_len + 1);
        if (rc != HEGEL_OK) {
            free(buf);
            atomic_store(&conn->closed, true);
            return rc;
        }
    }

    /* Decode the full packet */
    rc = hegel_packet_decode(buf, total, pkt);
    free(buf);
    return rc;
}

int hegel_connection_send_packet(hegel_connection *conn, const hegel_packet *pkt)
{
    if (atomic_load(&conn->closed))
        return HEGEL_ERR_CLOSED;

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(pkt, &buf, &len);
    if (rc != HEGEL_OK)
        return rc;

    pthread_mutex_lock(&conn->writer_mu);
    rc = hegel_write_exact(conn->socket_fd, buf, len);
    pthread_mutex_unlock(&conn->writer_mu);

    free(buf);
    if (rc != HEGEL_OK) {
        conn->last_error = HEGEL_ERR_IO;
    }
    return rc;
}

int hegel_send_packet(hegel_connection *conn, const hegel_packet *pkt)
{
    return hegel_connection_send_packet(conn, pkt);
}

int hegel_recv_packet(hegel_connection *conn, hegel_packet *pkt)
{
    pthread_mutex_lock(&conn->reader_mu);
    int rc = hegel_connection_read_packet(conn, pkt);
    pthread_mutex_unlock(&conn->reader_mu);
    return rc;
}

hegel_stream *hegel_connection_control_stream(hegel_connection *conn)
{
    if (!conn)
        return NULL;
    return conn->control_stream;
}

hegel_stream *hegel_connection_new_stream(hegel_connection *conn)
{
    if (!conn)
        return NULL;
    if (!conn->handshake_done) {
        conn->last_error = HEGEL_ERR_HANDSHAKE;
        return NULL;
    }

    /* Client streams use odd IDs: (counter << 1) | 1 */
    uint_fast32_t counter = atomic_fetch_add(&conn->next_stream_counter, 1);
    uint32_t stream_id = (uint32_t)((counter << 1) | 1);

    hegel_stream *stream = hegel_stream_new(conn, stream_id);
    if (!stream)
        return NULL;

    int rc = hegel_connection_register_stream(conn, stream);
    if (rc != HEGEL_OK) {
        hegel_stream_free(stream);
        return NULL;
    }

    return stream;
}

hegel_stream *hegel_connection_connect_stream(hegel_connection *conn, uint32_t stream_id)
{
    if (!conn)
        return NULL;
    if (!conn->handshake_done) {
        conn->last_error = HEGEL_ERR_HANDSHAKE;
        return NULL;
    }

    /* Check that the stream is not already registered */
    hegel_stream *existing = hegel_connection_find_stream(conn, stream_id);
    if (existing)
        return NULL;

    hegel_stream *stream = hegel_stream_new(conn, stream_id);
    if (!stream)
        return NULL;

    int rc = hegel_connection_register_stream(conn, stream);
    if (rc != HEGEL_OK) {
        hegel_stream_free(stream);
        return NULL;
    }

    return stream;
}

int hegel_connection_handshake(hegel_connection *conn)
{
    if (!conn)
        return HEGEL_ERR_PROTOCOL;
    if (conn->handshake_done) {
        conn->last_error = HEGEL_ERR_HANDSHAKE;
        return HEGEL_ERR_HANDSHAKE;
    }

    /* Send handshake request on the control stream (stream 0) */
    uint32_t msg_id;
    int rc = hegel_stream_send_raw(conn->control_stream,
                                   HANDSHAKE_REQUEST, HANDSHAKE_REQUEST_LEN,
                                   &msg_id);
    if (rc != HEGEL_OK) {
        conn->last_error = rc;
        return rc;
    }

    /* Wait for reply */
    hegel_packet reply;
    memset(&reply, 0, sizeof(reply));
    rc = hegel_stream_recv_reply(conn->control_stream, msg_id, &reply);
    if (rc != HEGEL_OK) {
        conn->last_error = rc;
        return rc;
    }

    /* Validate: reply payload should be "Hegel/{version}" */
    if (reply.payload_len < HANDSHAKE_PREFIX_LEN ||
        memcmp(reply.payload, HANDSHAKE_PREFIX, HANDSHAKE_PREFIX_LEN) != 0) {
        hegel_packet_free(&reply);
        conn->last_error = HEGEL_ERR_HANDSHAKE;
        return HEGEL_ERR_HANDSHAKE;
    }

    hegel_packet_free(&reply);
    conn->handshake_done = true;
    return HEGEL_OK;
}

hegel_error hegel_connection_last_error(const hegel_connection *conn)
{
    if (!conn)
        return HEGEL_ERR_PROTOCOL;
    return conn->last_error;
}
