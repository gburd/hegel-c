#include "packet.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>
#include <arpa/inet.h>  /* htonl / ntohl */

int hegel_read_exact(int fd, uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return HEGEL_ERR_IO;
        }
        if (r == 0)
            return HEGEL_ERR_IO;  /* EOF / connection closed */
        total += (size_t)r;
    }
    return HEGEL_OK;
}

int hegel_write_exact(int fd, const uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return HEGEL_ERR_IO;
        }
        total += (size_t)w;
    }
    return HEGEL_OK;
}

/*
 * Store a uint32 in big-endian at dst.
 */
static void put_be32(uint8_t *dst, uint32_t val)
{
    uint32_t be = htonl(val);
    memcpy(dst, &be, 4);
}

/*
 * Read a uint32 from big-endian at src.
 */
static uint32_t get_be32(const uint8_t *src)
{
    uint32_t be;
    memcpy(&be, src, 4);
    return ntohl(be);
}

uint32_t hegel_compute_crc32(const uint8_t *header, const uint8_t *payload,
                             size_t payload_len)
{
    /* CRC32 over the 20-byte header (with bytes 4-7 zeroed) + payload */
    uint8_t hdr_copy[HEGEL_HEADER_SIZE];
    memcpy(hdr_copy, header, HEGEL_HEADER_SIZE);
    memset(hdr_copy + 4, 0, 4);  /* zero checksum field */

    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, hdr_copy, HEGEL_HEADER_SIZE);
    if (payload_len > 0 && payload != NULL)
        crc = crc32(crc, payload, (uInt)payload_len);
    return (uint32_t)crc;
}

int hegel_packet_encode(const hegel_packet *pkt, uint8_t **out_buf, size_t *out_len)
{
    if (!pkt || !out_buf || !out_len)
        return HEGEL_ERR_PROTOCOL;

    size_t total = HEGEL_HEADER_SIZE + pkt->payload_len + 1;  /* +1 for terminator */
    uint8_t *buf = malloc(total);
    if (!buf)
        return HEGEL_ERR_ALLOC;

    uint32_t wire_msg_id = pkt->message_id;
    if (pkt->is_reply)
        wire_msg_id |= HEGEL_REPLY_BIT;

    /* Build header with zeroed checksum for CRC computation */
    put_be32(buf + 0, HEGEL_MAGIC);
    put_be32(buf + 4, 0);  /* placeholder for crc */
    put_be32(buf + 8, pkt->stream_id);
    put_be32(buf + 12, wire_msg_id);
    put_be32(buf + 16, (uint32_t)pkt->payload_len);

    /* Copy payload */
    if (pkt->payload_len > 0 && pkt->payload != NULL)
        memcpy(buf + HEGEL_HEADER_SIZE, pkt->payload, pkt->payload_len);

    /* Compute CRC over header (with crc zeroed) + payload */
    uint32_t crc = hegel_compute_crc32(buf, buf + HEGEL_HEADER_SIZE, pkt->payload_len);
    put_be32(buf + 4, crc);

    /* Terminator */
    buf[total - 1] = HEGEL_TERMINATOR;

    *out_buf = buf;
    *out_len = total;
    return HEGEL_OK;
}

int hegel_packet_decode(const uint8_t *buf, size_t len, hegel_packet *pkt)
{
    if (!buf || !pkt)
        return HEGEL_ERR_PROTOCOL;
    if (len < HEGEL_HEADER_SIZE)
        return HEGEL_ERR_PROTOCOL;

    /* Parse header fields */
    uint32_t magic = get_be32(buf + 0);
    uint32_t checksum = get_be32(buf + 4);
    uint32_t stream_id = get_be32(buf + 8);
    uint32_t message_id_raw = get_be32(buf + 12);
    uint32_t payload_len = get_be32(buf + 16);

    if (magic != HEGEL_MAGIC)
        return HEGEL_ERR_BAD_MAGIC;

    /* Check total length: header + payload + terminator */
    size_t expected = (size_t)HEGEL_HEADER_SIZE + payload_len + 1;
    if (len < expected)
        return HEGEL_ERR_PROTOCOL;

    /* Verify terminator */
    if (buf[HEGEL_HEADER_SIZE + payload_len] != HEGEL_TERMINATOR)
        return HEGEL_ERR_BAD_TERMINATOR;

    /* Verify CRC */
    uint32_t computed = hegel_compute_crc32(buf, buf + HEGEL_HEADER_SIZE, payload_len);
    if (computed != checksum)
        return HEGEL_ERR_BAD_CRC;

    /* Extract reply bit */
    bool is_reply = (message_id_raw & HEGEL_REPLY_BIT) != 0;
    uint32_t message_id = message_id_raw & ~HEGEL_REPLY_BIT;

    /* Copy payload */
    uint8_t *payload = NULL;
    if (payload_len > 0) {
        payload = malloc(payload_len);
        if (!payload)
            return HEGEL_ERR_ALLOC;
        memcpy(payload, buf + HEGEL_HEADER_SIZE, payload_len);
    }

    pkt->stream_id = stream_id;
    pkt->message_id = message_id;
    pkt->is_reply = is_reply;
    pkt->payload = payload;
    pkt->payload_len = payload_len;
    return HEGEL_OK;
}

void hegel_packet_free(hegel_packet *pkt)
{
    if (pkt) {
        free(pkt->payload);
        pkt->payload = NULL;
        pkt->payload_len = 0;
    }
}
