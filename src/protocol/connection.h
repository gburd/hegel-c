#ifndef HEGEL_PROTOCOL_CONNECTION_H
#define HEGEL_PROTOCOL_CONNECTION_H

#include "hegel/protocol.h"

#include <pthread.h>
#include <stdatomic.h>

/*
 * Maximum number of streams per connection.
 * This is a practical upper bound; dynamic resizing could be added if needed.
 */
#define HEGEL_MAX_STREAMS 256

/*
 * Stream registry entry.
 */
typedef struct {
    uint32_t stream_id;
    hegel_stream *stream;
    bool occupied;
} hegel_stream_entry;

/*
 * Connection state: manages pipe I/O, stream registry, and thread safety.
 *
 * Uses a demand-driven reader model: no background thread. When a stream
 * needs a packet, the calling thread acquires the reader lock, reads from
 * the pipe, and dispatches packets to stream inboxes.
 */
struct hegel_connection {
    int read_fd;
    int write_fd;

    /* Stream counter for allocating odd client stream IDs */
    atomic_uint_fast32_t next_stream_counter;

    /* Writer mutex: only one thread writes at a time */
    pthread_mutex_t writer_mu;

    /* Reader mutex: only one thread reads at a time */
    pthread_mutex_t reader_mu;

    /* Stream registry */
    hegel_stream_entry streams[HEGEL_MAX_STREAMS];
    pthread_mutex_t streams_mu;

    /* Control stream (stream 0) */
    hegel_stream *control_stream;

    /* Whether handshake has been completed */
    bool handshake_done;

    /* Last error */
    hegel_error last_error;

    /* Whether the connection is closed / server exited */
    atomic_bool closed;
};

/*
 * Register a stream in the connection's registry.
 * Caller must hold streams_mu, or only call from single-threaded context.
 */
int hegel_connection_register_stream(hegel_connection *conn, hegel_stream *stream);

/*
 * Unregister a stream from the registry.
 */
void hegel_connection_unregister_stream(hegel_connection *conn, uint32_t stream_id);

/*
 * Find a stream by its ID. Returns NULL if not found.
 */
hegel_stream *hegel_connection_find_stream(hegel_connection *conn, uint32_t stream_id);

/*
 * Read one packet from the read_fd (called under reader_mu).
 * Fills *pkt. Returns HEGEL_OK or error.
 */
int hegel_connection_read_packet(hegel_connection *conn, hegel_packet *pkt);

/*
 * Send a packet on the connection (acquires writer_mu).
 */
int hegel_connection_send_packet(hegel_connection *conn, const hegel_packet *pkt);

#endif /* HEGEL_PROTOCOL_CONNECTION_H */
