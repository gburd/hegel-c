#ifndef HEGEL_PROTOCOL_STREAM_H
#define HEGEL_PROTOCOL_STREAM_H

#include "hegel/protocol.h"
#include "connection.h"

#include <pthread.h>

/*
 * Maximum pending packets in a stream's inbox.
 */
#define HEGEL_STREAM_INBOX_SIZE 128

/*
 * Node in the stream's packet inbox (ring buffer).
 */
typedef struct {
    hegel_packet packets[HEGEL_STREAM_INBOX_SIZE];
    size_t head;    /* index of next packet to consume */
    size_t tail;    /* index of next free slot */
    size_t count;   /* number of packets in the buffer */
} hegel_inbox;

/*
 * Stream: a multiplexed logical channel over a connection.
 *
 * Each stream has its own inbox for pending packets, separated into
 * responses (replies to our requests) and requests (server-initiated).
 */
struct hegel_stream {
    hegel_connection *conn;
    uint32_t stream_id;
    uint32_t next_message_id;

    /* Inbox for reply packets (keyed by message_id) */
    hegel_packet responses[HEGEL_STREAM_INBOX_SIZE];
    size_t response_count;

    /* Inbox for request packets (server-initiated) */
    hegel_packet requests[HEGEL_STREAM_INBOX_SIZE];
    size_t request_count;

    /* Mutex protecting the inbox arrays */
    pthread_mutex_t inbox_mu;

    bool closed;
};

/*
 * Create a new stream object. Does not register it with the connection.
 */
hegel_stream *hegel_stream_new(hegel_connection *conn, uint32_t stream_id);

/*
 * Dispatch a packet to the appropriate slot in this stream's inbox.
 * Caller must hold stream->inbox_mu.
 */
int hegel_stream_deliver(hegel_stream *stream, const hegel_packet *pkt);

/*
 * Low-level: send a raw payload as a request. Returns the message_id used.
 * Sets *out_msg_id to the assigned message ID.
 */
int hegel_stream_send_raw(hegel_stream *stream, const uint8_t *payload,
                          size_t payload_len, uint32_t *out_msg_id);

/*
 * Low-level: send a raw reply to a specific message_id.
 */
int hegel_stream_send_reply_raw(hegel_stream *stream, uint32_t message_id,
                                const uint8_t *payload, size_t payload_len);

/*
 * Wait for a reply to a specific message_id.
 * Uses the demand-driven reader pattern: if the reply isn't in the inbox,
 * reads from the socket and dispatches packets until the reply arrives.
 * Fills *pkt with the reply packet.
 */
int hegel_stream_recv_reply(hegel_stream *stream, uint32_t message_id, hegel_packet *pkt);

/*
 * Wait for the next unrequested (server-initiated) packet.
 * Uses the demand-driven reader pattern.
 * Fills *pkt with the request packet.
 */
int hegel_stream_recv_request(hegel_stream *stream, hegel_packet *pkt);

#endif /* HEGEL_PROTOCOL_STREAM_H */
