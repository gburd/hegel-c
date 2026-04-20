#include "stream.h"
#include "packet.h"
#include "cbor_helpers.h"

#include <stdlib.h>
#include <string.h>

hegel_stream *hegel_stream_new(hegel_connection *conn, uint32_t stream_id)
{
    hegel_stream *stream = calloc(1, sizeof(hegel_stream));
    if (!stream)
        return NULL;

    stream->conn = conn;
    stream->stream_id = stream_id;
    stream->next_message_id = 1;
    stream->response_count = 0;
    stream->request_count = 0;
    stream->closed = false;

    if (pthread_mutex_init(&stream->inbox_mu, NULL) != 0) {
        free(stream);
        return NULL;
    }

    return stream;
}

void hegel_stream_free(hegel_stream *stream)
{
    if (!stream)
        return;

    /* Free any pending packets in the inbox */
    for (size_t i = 0; i < stream->response_count; i++)
        hegel_packet_free(&stream->responses[i]);
    for (size_t i = 0; i < stream->request_count; i++)
        hegel_packet_free(&stream->requests[i]);

    pthread_mutex_destroy(&stream->inbox_mu);
    free(stream);
}

uint32_t hegel_stream_id(const hegel_stream *stream)
{
    if (!stream)
        return 0;
    return stream->stream_id;
}

int hegel_stream_deliver(hegel_stream *stream, const hegel_packet *pkt)
{
    /* Duplicate the packet payload so the stream owns its copy */
    hegel_packet copy;
    copy.stream_id = pkt->stream_id;
    copy.message_id = pkt->message_id;
    copy.is_reply = pkt->is_reply;
    copy.payload_len = pkt->payload_len;
    copy.payload = NULL;

    if (pkt->payload_len > 0 && pkt->payload != NULL) {
        copy.payload = malloc(pkt->payload_len);
        if (!copy.payload)
            return HEGEL_ERR_ALLOC;
        memcpy(copy.payload, pkt->payload, pkt->payload_len);
    }

    if (pkt->is_reply) {
        if (stream->response_count >= HEGEL_STREAM_INBOX_SIZE) {
            free(copy.payload);
            return HEGEL_ERR_ALLOC;  /* inbox full */
        }
        stream->responses[stream->response_count++] = copy;
    } else {
        if (stream->request_count >= HEGEL_STREAM_INBOX_SIZE) {
            free(copy.payload);
            return HEGEL_ERR_ALLOC;  /* inbox full */
        }
        stream->requests[stream->request_count++] = copy;
    }

    return HEGEL_OK;
}

int hegel_stream_send_raw(hegel_stream *stream, const uint8_t *payload,
                          size_t payload_len, uint32_t *out_msg_id)
{
    if (!stream || stream->closed)
        return HEGEL_ERR_CLOSED;

    uint32_t msg_id = stream->next_message_id++;

    hegel_packet pkt;
    pkt.stream_id = stream->stream_id;
    pkt.message_id = msg_id;
    pkt.is_reply = false;
    pkt.payload = (uint8_t *)payload;  /* hegel_connection_send_packet only reads */
    pkt.payload_len = payload_len;

    int rc = hegel_connection_send_packet(stream->conn, &pkt);
    if (rc != HEGEL_OK)
        return rc;

    if (out_msg_id)
        *out_msg_id = msg_id;
    return HEGEL_OK;
}

int hegel_stream_send_reply_raw(hegel_stream *stream, uint32_t message_id,
                                const uint8_t *payload, size_t payload_len)
{
    if (!stream || stream->closed)
        return HEGEL_ERR_CLOSED;

    hegel_packet pkt;
    pkt.stream_id = stream->stream_id;
    pkt.message_id = message_id;
    pkt.is_reply = true;
    pkt.payload = (uint8_t *)payload;
    pkt.payload_len = payload_len;

    return hegel_connection_send_packet(stream->conn, &pkt);
}

/*
 * Demand-driven reader: read packets from the socket, dispatching them to
 * the correct stream's inbox. Continues until the desired condition is met.
 *
 * This must be called with conn->reader_mu held.
 */
static int read_and_dispatch_one(hegel_connection *conn, hegel_packet *out_pkt)
{
    int rc = hegel_connection_read_packet(conn, out_pkt);
    if (rc != HEGEL_OK)
        return rc;

    return HEGEL_OK;
}

/*
 * Dispatch a packet to the correct stream. If it belongs to another stream,
 * deliver it to that stream's inbox.
 * Returns true if the packet was for `target_stream_id`.
 */
static bool dispatch_packet(hegel_connection *conn, hegel_packet *pkt,
                            uint32_t target_stream_id)
{
    if (pkt->stream_id == target_stream_id) {
        return true;  /* caller handles it */
    }

    /* Find the target stream and deliver */
    hegel_stream *target = hegel_connection_find_stream(conn, pkt->stream_id);
    if (target) {
        pthread_mutex_lock(&target->inbox_mu);
        hegel_stream_deliver(target, pkt);
        pthread_mutex_unlock(&target->inbox_mu);
    }
    /* Free the dispatched packet (deliver makes a copy) */
    hegel_packet_free(pkt);
    return false;
}

int hegel_stream_recv_reply(hegel_stream *stream, uint32_t message_id, hegel_packet *out)
{
    if (!stream || !out)
        return HEGEL_ERR_PROTOCOL;

    /* First, check inbox for an already-received reply */
    pthread_mutex_lock(&stream->inbox_mu);
    for (size_t i = 0; i < stream->response_count; i++) {
        if (stream->responses[i].message_id == message_id) {
            *out = stream->responses[i];
            /* Remove from responses by shifting */
            for (size_t j = i; j + 1 < stream->response_count; j++)
                stream->responses[j] = stream->responses[j + 1];
            stream->response_count--;
            pthread_mutex_unlock(&stream->inbox_mu);
            return HEGEL_OK;
        }
    }
    pthread_mutex_unlock(&stream->inbox_mu);

    /* Not found -- read from socket until we get our reply */
    pthread_mutex_lock(&stream->conn->reader_mu);
    for (;;) {
        /* Re-check inbox (another thread may have dispatched our packet) */
        pthread_mutex_lock(&stream->inbox_mu);
        for (size_t i = 0; i < stream->response_count; i++) {
            if (stream->responses[i].message_id == message_id) {
                *out = stream->responses[i];
                for (size_t j = i; j + 1 < stream->response_count; j++)
                    stream->responses[j] = stream->responses[j + 1];
                stream->response_count--;
                pthread_mutex_unlock(&stream->inbox_mu);
                pthread_mutex_unlock(&stream->conn->reader_mu);
                return HEGEL_OK;
            }
        }
        pthread_mutex_unlock(&stream->inbox_mu);

        /* Read one packet from the socket */
        hegel_packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        int rc = read_and_dispatch_one(stream->conn, &pkt);
        if (rc != HEGEL_OK) {
            pthread_mutex_unlock(&stream->conn->reader_mu);
            return rc;
        }

        if (dispatch_packet(stream->conn, &pkt, stream->stream_id)) {
            /* Packet is for us */
            if (pkt.is_reply && pkt.message_id == message_id) {
                /* This is the reply we're waiting for */
                *out = pkt;
                pthread_mutex_unlock(&stream->conn->reader_mu);
                return HEGEL_OK;
            }
            /* It's for us but not the reply we want -- stash it */
            pthread_mutex_lock(&stream->inbox_mu);
            hegel_stream_deliver(stream, &pkt);
            pthread_mutex_unlock(&stream->inbox_mu);
            hegel_packet_free(&pkt);
        }
    }
}

int hegel_stream_recv_request(hegel_stream *stream, hegel_packet *out)
{
    if (!stream || !out)
        return HEGEL_ERR_PROTOCOL;

    /* Check inbox first */
    pthread_mutex_lock(&stream->inbox_mu);
    if (stream->request_count > 0) {
        *out = stream->requests[0];
        for (size_t j = 0; j + 1 < stream->request_count; j++)
            stream->requests[j] = stream->requests[j + 1];
        stream->request_count--;
        pthread_mutex_unlock(&stream->inbox_mu);
        return HEGEL_OK;
    }
    pthread_mutex_unlock(&stream->inbox_mu);

    /* Read from socket until we get a request for this stream */
    pthread_mutex_lock(&stream->conn->reader_mu);
    for (;;) {
        /* Re-check inbox */
        pthread_mutex_lock(&stream->inbox_mu);
        if (stream->request_count > 0) {
            *out = stream->requests[0];
            for (size_t j = 0; j + 1 < stream->request_count; j++)
                stream->requests[j] = stream->requests[j + 1];
            stream->request_count--;
            pthread_mutex_unlock(&stream->inbox_mu);
            pthread_mutex_unlock(&stream->conn->reader_mu);
            return HEGEL_OK;
        }
        pthread_mutex_unlock(&stream->inbox_mu);

        hegel_packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        int rc = read_and_dispatch_one(stream->conn, &pkt);
        if (rc != HEGEL_OK) {
            pthread_mutex_unlock(&stream->conn->reader_mu);
            return rc;
        }

        if (dispatch_packet(stream->conn, &pkt, stream->stream_id)) {
            /* Packet is for us */
            if (!pkt.is_reply) {
                /* This is a request -- exactly what we want */
                *out = pkt;
                pthread_mutex_unlock(&stream->conn->reader_mu);
                return HEGEL_OK;
            }
            /* It's a reply for us, stash it */
            pthread_mutex_lock(&stream->inbox_mu);
            hegel_stream_deliver(stream, &pkt);
            pthread_mutex_unlock(&stream->inbox_mu);
            hegel_packet_free(&pkt);
        }
    }
}

/*
 * High-level: send a CBOR request and wait for the CBOR reply.
 * Returns the "result" value from the reply, or NULL on error.
 * The caller owns the returned cbor_item_t.
 */
cbor_item_t *hegel_stream_request(hegel_stream *stream, cbor_item_t *request)
{
    if (!stream || !request)
        return NULL;

    /* Serialize CBOR request */
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    int rc = cbor_serialize_alloc_checked(request, &payload, &payload_len);
    if (rc != HEGEL_OK)
        return NULL;

    /* Send the request */
    uint32_t msg_id;
    rc = hegel_stream_send_raw(stream, payload, payload_len, &msg_id);
    free(payload);
    if (rc != HEGEL_OK)
        return NULL;

    /* Wait for the reply */
    hegel_packet reply;
    memset(&reply, 0, sizeof(reply));
    rc = hegel_stream_recv_reply(stream, msg_id, &reply);
    if (rc != HEGEL_OK)
        return NULL;

    /* Decode the CBOR reply */
    struct cbor_load_result load_result;
    cbor_item_t *reply_item = cbor_load(reply.payload, reply.payload_len, &load_result);
    hegel_packet_free(&reply);

    if (!reply_item || load_result.error.code != CBOR_ERR_NONE) {
        if (reply_item)
            cbor_decref(&reply_item);
        return NULL;
    }

    /* Check for error key */
    cbor_item_t *error_val = cbor_map_get(reply_item, "error");
    if (error_val) {
        /* Server returned an error */
        cbor_decref(&reply_item);
        return NULL;
    }

    /* Extract "result" value */
    cbor_item_t *result_val = cbor_map_get(reply_item, "result");
    if (result_val) {
        cbor_incref(result_val);
        cbor_decref(&reply_item);
        return result_val;
    }

    /* If no "result" key, return the whole reply */
    return reply_item;
}

cbor_item_t *hegel_stream_recv_event(hegel_stream *stream, uint32_t *out_message_id)
{
    if (!stream)
        return NULL;

    hegel_packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    int rc = hegel_stream_recv_request(stream, &pkt);
    if (rc != HEGEL_OK)
        return NULL;

    if (out_message_id)
        *out_message_id = pkt.message_id;

    /* Decode CBOR */
    struct cbor_load_result load_result;
    cbor_item_t *item = cbor_load(pkt.payload, pkt.payload_len, &load_result);
    hegel_packet_free(&pkt);

    if (!item || load_result.error.code != CBOR_ERR_NONE) {
        if (item)
            cbor_decref(&item);
        return NULL;
    }

    return item;
}

int hegel_stream_reply_event(hegel_stream *stream, uint32_t message_id, cbor_item_t *reply)
{
    if (!stream || !reply)
        return HEGEL_ERR_PROTOCOL;

    /* Serialize reply */
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    int rc = cbor_serialize_alloc_checked(reply, &payload, &payload_len);
    if (rc != HEGEL_OK)
        return rc;

    rc = hegel_stream_send_reply_raw(stream, message_id, payload, payload_len);
    free(payload);
    return rc;
}
