#ifndef HEGEL_PROTOCOL_H
#define HEGEL_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <cbor.h>

/* Protocol constants */
#define HEGEL_MAGIC 0x4845474CU  /* "HEGL" */
#define HEGEL_HEADER_SIZE 20
#define HEGEL_TERMINATOR 0x0A
#define HEGEL_REPLY_BIT (1U << 31)
#define HEGEL_STREAM_TIMEOUT_MS 30000
#define HEGEL_CLOSE_STREAM_MESSAGE_ID ((1U << 31) - 1)

/* Forward declarations */
typedef struct hegel_connection hegel_connection;
typedef struct hegel_stream hegel_stream;

/* Packet structure */
typedef struct {
    uint32_t stream_id;
    uint32_t message_id;
    bool is_reply;
    uint8_t *payload;
    size_t payload_len;
} hegel_packet;

/* Error codes */
typedef enum {
    HEGEL_OK = 0,                  /* Success */
    HEGEL_ERR_ALLOC = -1,          /* Memory allocation failed */
    HEGEL_ERR_IO = -2,             /* Socket read/write error */
    HEGEL_ERR_PROTOCOL = -3,       /* Unexpected protocol state or message */
    HEGEL_ERR_BAD_MAGIC = -4,      /* Header magic bytes != "HEGL" */
    HEGEL_ERR_BAD_CRC = -5,        /* Payload CRC32 mismatch */
    HEGEL_ERR_BAD_TERMINATOR = -6, /* Missing 0x0A terminator byte */
    HEGEL_ERR_HANDSHAKE = -7,      /* Version negotiation failed */
    HEGEL_ERR_CLOSED = -8,         /* Connection or stream already closed */
    HEGEL_ERR_TIMEOUT = -9,        /* Response not received within deadline */
    HEGEL_ERR_SERVER = -10,        /* Server reported an error */
    HEGEL_ERR_CBOR = -11           /* CBOR encode/decode failure */
} hegel_error;

/* Connection management */
hegel_connection *hegel_connection_new(int read_fd, int write_fd);
void hegel_connection_free(hegel_connection *conn);
int hegel_connection_handshake(hegel_connection *conn);

/* Stream management */
hegel_stream *hegel_connection_control_stream(hegel_connection *conn);
hegel_stream *hegel_connection_new_stream(hegel_connection *conn);
hegel_stream *hegel_connection_connect_stream(hegel_connection *conn, uint32_t stream_id);
void hegel_stream_free(hegel_stream *stream);
uint32_t hegel_stream_id(const hegel_stream *stream);

/* Request/reply (sends CBOR request, blocks for reply) */
cbor_item_t *hegel_stream_request(hegel_stream *stream, cbor_item_t *request);

/* Low-level: send/receive packets */
int hegel_send_packet(hegel_connection *conn, const hegel_packet *pkt);
int hegel_recv_packet(hegel_connection *conn, hegel_packet *pkt);
void hegel_packet_free(hegel_packet *pkt);

/* Wait for next server-initiated request on a stream */
cbor_item_t *hegel_stream_recv_event(hegel_stream *stream, uint32_t *out_message_id);
/* Reply to a server event */
int hegel_stream_reply_event(hegel_stream *stream, uint32_t message_id, cbor_item_t *reply);

/* Get last error code (per-connection) */
hegel_error hegel_connection_last_error(const hegel_connection *conn);

/*
 * Return the effective stream timeout in milliseconds.
 * Checks HEGEL_STREAM_TIMEOUT env var, falls back to HEGEL_STREAM_TIMEOUT_MS.
 */
int hegel_stream_timeout_ms(void);

#endif /* HEGEL_PROTOCOL_H */
