#ifndef HEGEL_PROTOCOL_PACKET_H
#define HEGEL_PROTOCOL_PACKET_H

#include "hegel/protocol.h"

/*
 * Encode a packet into a wire-format buffer.
 * Allocates *out_buf (caller must free). Sets *out_len to the total size.
 * Returns HEGEL_OK on success.
 */
int hegel_packet_encode(const hegel_packet *pkt, uint8_t **out_buf, size_t *out_len);

/*
 * Decode a packet from a wire-format buffer.
 * buf must contain at least HEGEL_HEADER_SIZE + payload_len + 1 bytes.
 * Allocates pkt->payload (caller must call hegel_packet_free).
 * Returns HEGEL_OK on success.
 */
int hegel_packet_decode(const uint8_t *buf, size_t len, hegel_packet *pkt);

/*
 * Compute CRC32 over a 20-byte header (with crc field zeroed) + payload.
 */
uint32_t hegel_compute_crc32(const uint8_t *header, const uint8_t *payload, size_t payload_len);

/*
 * Read exactly n bytes from fd into buf.
 * Returns HEGEL_OK on success, HEGEL_ERR_IO on failure/short read.
 */
int hegel_read_exact(int fd, uint8_t *buf, size_t n);

/*
 * Write exactly n bytes from buf to fd.
 * Returns HEGEL_OK on success, HEGEL_ERR_IO on failure.
 */
int hegel_write_exact(int fd, const uint8_t *buf, size_t n);

#endif /* HEGEL_PROTOCOL_PACKET_H */
