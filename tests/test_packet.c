/*
 * Unit tests for packet encoding/decoding.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <string.h>
#include <stdlib.h>

#include "hegel/protocol.h"
#include "protocol/packet.h"

/* Test that encoding produces the correct magic bytes at the start. */
static void test_packet_magic(void **state)
{
    (void)state;

    hegel_packet pkt = {
        .stream_id = 0,
        .message_id = 1,
        .is_reply = false,
        .payload = NULL,
        .payload_len = 0
    };

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(&pkt, &buf, &len);
    assert_int_equal(rc, HEGEL_OK);
    assert_non_null(buf);

    /* First 4 bytes are the magic value 0x4845474C ("HEGL") in big-endian */
    assert_int_equal(buf[0], 0x48);
    assert_int_equal(buf[1], 0x45);
    assert_int_equal(buf[2], 0x47);
    assert_int_equal(buf[3], 0x4C);

    /* Total size: 20-byte header + 0 payload + 1 terminator */
    assert_int_equal(len, HEGEL_HEADER_SIZE + 0 + 1);

    /* Terminator at the end */
    assert_int_equal(buf[len - 1], HEGEL_TERMINATOR);

    free(buf);
}

/* Test round-trip: encode then decode should produce the same field values. */
static void test_packet_round_trip(void **state)
{
    (void)state;

    uint8_t payload_data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};

    hegel_packet orig = {
        .stream_id = 42,
        .message_id = 7,
        .is_reply = false,
        .payload = payload_data,
        .payload_len = sizeof(payload_data)
    };

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(&orig, &buf, &len);
    assert_int_equal(rc, HEGEL_OK);
    assert_non_null(buf);
    assert_int_equal(len, HEGEL_HEADER_SIZE + sizeof(payload_data) + 1);

    /* Decode */
    hegel_packet decoded;
    memset(&decoded, 0, sizeof(decoded));
    rc = hegel_packet_decode(buf, len, &decoded);
    assert_int_equal(rc, HEGEL_OK);

    assert_int_equal(decoded.stream_id, orig.stream_id);
    assert_int_equal(decoded.message_id, orig.message_id);
    assert_false(decoded.is_reply);
    assert_int_equal(decoded.payload_len, orig.payload_len);
    assert_memory_equal(decoded.payload, orig.payload, orig.payload_len);

    hegel_packet_free(&decoded);
    free(buf);
}

/* Test round-trip with reply bit set. */
static void test_packet_round_trip_reply(void **state)
{
    (void)state;

    uint8_t payload_data[] = {0x01, 0x02, 0x03};

    hegel_packet orig = {
        .stream_id = 5,
        .message_id = 99,
        .is_reply = true,
        .payload = payload_data,
        .payload_len = sizeof(payload_data)
    };

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(&orig, &buf, &len);
    assert_int_equal(rc, HEGEL_OK);

    hegel_packet decoded;
    memset(&decoded, 0, sizeof(decoded));
    rc = hegel_packet_decode(buf, len, &decoded);
    assert_int_equal(rc, HEGEL_OK);

    assert_int_equal(decoded.stream_id, 5);
    assert_int_equal(decoded.message_id, 99);
    assert_true(decoded.is_reply);
    assert_int_equal(decoded.payload_len, 3);

    hegel_packet_free(&decoded);
    free(buf);
}

/* Test that CRC32 validation detects corruption. */
static void test_packet_crc32(void **state)
{
    (void)state;

    uint8_t payload_data[] = {0xCA, 0xFE};

    hegel_packet orig = {
        .stream_id = 1,
        .message_id = 2,
        .is_reply = false,
        .payload = payload_data,
        .payload_len = sizeof(payload_data)
    };

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(&orig, &buf, &len);
    assert_int_equal(rc, HEGEL_OK);

    /* Verify the original decodes successfully */
    hegel_packet decoded;
    memset(&decoded, 0, sizeof(decoded));
    rc = hegel_packet_decode(buf, len, &decoded);
    assert_int_equal(rc, HEGEL_OK);
    hegel_packet_free(&decoded);

    /* Corrupt the payload byte */
    buf[HEGEL_HEADER_SIZE] ^= 0xFF;

    memset(&decoded, 0, sizeof(decoded));
    rc = hegel_packet_decode(buf, len, &decoded);
    assert_int_equal(rc, HEGEL_ERR_BAD_CRC);

    free(buf);
}

/* Test that decoding detects bad magic. */
static void test_packet_bad_magic(void **state)
{
    (void)state;

    hegel_packet orig = {
        .stream_id = 0,
        .message_id = 0,
        .is_reply = false,
        .payload = NULL,
        .payload_len = 0
    };

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(&orig, &buf, &len);
    assert_int_equal(rc, HEGEL_OK);

    /* Corrupt the magic */
    buf[0] = 0xFF;

    hegel_packet decoded;
    memset(&decoded, 0, sizeof(decoded));
    rc = hegel_packet_decode(buf, len, &decoded);
    assert_int_equal(rc, HEGEL_ERR_BAD_MAGIC);

    free(buf);
}

/* Test that decoding detects bad terminator. */
static void test_packet_bad_terminator(void **state)
{
    (void)state;

    hegel_packet orig = {
        .stream_id = 0,
        .message_id = 0,
        .is_reply = false,
        .payload = NULL,
        .payload_len = 0
    };

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(&orig, &buf, &len);
    assert_int_equal(rc, HEGEL_OK);

    /* Corrupt the terminator. We need to also fix the CRC since the terminator
     * is checked before CRC in the decode function... actually looking at the
     * code, CRC is checked before the terminator if the length check passes.
     * Let's just change the terminator. Since the terminator is outside the
     * CRC computation, we can corrupt it independently. */
    buf[len - 1] = 0xFF;

    hegel_packet decoded;
    memset(&decoded, 0, sizeof(decoded));
    rc = hegel_packet_decode(buf, len, &decoded);
    assert_int_equal(rc, HEGEL_ERR_BAD_TERMINATOR);

    free(buf);
}

/* Test encoding/decoding with empty payload. */
static void test_packet_empty_payload(void **state)
{
    (void)state;

    hegel_packet orig = {
        .stream_id = 0xFFFFFFFF,
        .message_id = 0x7FFFFFFF,
        .is_reply = false,
        .payload = NULL,
        .payload_len = 0
    };

    uint8_t *buf = NULL;
    size_t len = 0;
    int rc = hegel_packet_encode(&orig, &buf, &len);
    assert_int_equal(rc, HEGEL_OK);
    assert_int_equal(len, HEGEL_HEADER_SIZE + 1);

    hegel_packet decoded;
    memset(&decoded, 0, sizeof(decoded));
    rc = hegel_packet_decode(buf, len, &decoded);
    assert_int_equal(rc, HEGEL_OK);
    assert_int_equal(decoded.stream_id, 0xFFFFFFFF);
    assert_int_equal(decoded.message_id, 0x7FFFFFFF);
    assert_null(decoded.payload);
    assert_int_equal(decoded.payload_len, 0);

    hegel_packet_free(&decoded);
    free(buf);
}

/* Test decode with truncated buffer. */
static void test_packet_decode_truncated(void **state)
{
    (void)state;

    hegel_packet decoded;
    memset(&decoded, 0, sizeof(decoded));

    /* Buffer shorter than header */
    uint8_t short_buf[10] = {0};
    int rc = hegel_packet_decode(short_buf, sizeof(short_buf), &decoded);
    assert_int_equal(rc, HEGEL_ERR_PROTOCOL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_packet_magic),
        cmocka_unit_test(test_packet_round_trip),
        cmocka_unit_test(test_packet_round_trip_reply),
        cmocka_unit_test(test_packet_crc32),
        cmocka_unit_test(test_packet_bad_magic),
        cmocka_unit_test(test_packet_bad_terminator),
        cmocka_unit_test(test_packet_empty_payload),
        cmocka_unit_test(test_packet_decode_truncated),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
