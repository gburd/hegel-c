#include "generator.h"
#include "../protocol/cbor_helpers.h"
#include "../protocol/stream.h"
#include "../test_case.h"
#include "hegel/protocol.h"
#include "hegel/hegel.h"

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

/* Access the stream from a test case (defined in test_case.h) */
static hegel_stream *hegel_test_case_stream(hegel_test_case *tc)
{
    return tc ? tc->stream : NULL;
}

/*
 * Send a CBOR request on the test case stream and handle StopTest errors.
 * Returns the "result" from the reply, or NULL.
 * On StopTest, performs longjmp out of the test case.
 */
static cbor_item_t *send_tc_request(hegel_test_case *tc, cbor_item_t *request)
{
    hegel_stream *s = hegel_test_case_stream(tc);
    if (!s || !request)
        return NULL;

    /* Serialize and send */
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    int rc = cbor_serialize_alloc_checked(request, &payload, &payload_len);
    if (rc != HEGEL_OK)
        return NULL;

    uint32_t msg_id;
    rc = hegel_stream_send_raw(s, payload, payload_len, &msg_id);
    free(payload);
    if (rc != HEGEL_OK)
        return NULL;

    /* Wait for reply */
    hegel_packet reply_pkt;
    memset(&reply_pkt, 0, sizeof(reply_pkt));
    rc = hegel_stream_recv_reply(s, msg_id, &reply_pkt);
    if (rc != HEGEL_OK)
        return NULL;

    /* Decode CBOR reply */
    struct cbor_load_result load_result;
    cbor_item_t *reply = cbor_load(reply_pkt.payload, reply_pkt.payload_len, &load_result);
    hegel_packet_free(&reply_pkt);

    if (!reply || load_result.error.code != CBOR_ERR_NONE) {
        if (reply)
            cbor_decref(&reply);
        return NULL;
    }

    /* Check for error key */
    cbor_item_t *error_val = cbor_map_get(reply, "error");
    if (error_val) {
        char *error_str = cbor_get_string(error_val);
        if (error_str && strcmp(error_str, "StopTest") == 0) {
            free(error_str);
            cbor_decref(&reply);
            /* StopTest: longjmp out */
            tc->aborted = true;
            tc->jmp_reason = HEGEL_JMP_STOP_TEST;
            longjmp(tc->escape_jmp, HEGEL_JMP_STOP_TEST);
        }
        free(error_str);
        cbor_decref(&reply);
        return NULL;
    }

    /* Extract "result" value */
    cbor_item_t *result_val = cbor_map_get(reply, "result");
    if (result_val) {
        cbor_incref(result_val);
        cbor_decref(&reply);
        return result_val;
    }

    /* No result key -- return entire reply */
    return reply;
}

/* ================================================================
 * Generator allocation / free
 * ================================================================ */

hegel_generator *hegel_generator_alloc(hegel_gen_vtable vtable, void *data)
{
    hegel_generator *gen = calloc(1, sizeof(hegel_generator));
    if (!gen)
        return NULL;
    gen->vtable = vtable;
    gen->data = data;
    return gen;
}

void hegel_generator_free(hegel_generator *gen)
{
    if (!gen)
        return;
    if (gen->vtable.free)
        gen->vtable.free(gen);
    else
        free(gen);
}

/* ================================================================
 * Protocol helpers
 * ================================================================ */

void hegel_send_start_span(hegel_test_case *tc, int label)
{
    /* Build: {"command": "start_span", "label": <label>} */
    cbor_item_t *cmd = cbor_new_definite_map(2);
    if (!cmd)
        return;
    cbor_map_add_string(cmd, "command", "start_span");
    cbor_map_add_int(cmd, "label", (int64_t)label);

    cbor_item_t *reply = send_tc_request(tc, cmd);
    cbor_decref(&cmd);
    if (reply)
        cbor_decref(&reply);
}

void hegel_send_stop_span(hegel_test_case *tc, bool discard)
{
    /* Build: {"command": "stop_span", "discard": <discard>} */
    cbor_item_t *cmd = cbor_new_definite_map(2);
    if (!cmd)
        return;
    cbor_map_add_string(cmd, "command", "stop_span");
    cbor_map_add_bool(cmd, "discard", discard);

    cbor_item_t *reply = send_tc_request(tc, cmd);
    cbor_decref(&cmd);
    if (reply)
        cbor_decref(&reply);
}

cbor_item_t *hegel_send_generate(hegel_test_case *tc, cbor_item_t *schema)
{
    /* Build: {"command": "generate", "schema": <schema>} */
    cbor_item_t *cmd = cbor_new_definite_map(2);
    if (!cmd)
        return NULL;
    cbor_map_add_string(cmd, "command", "generate");
    cbor_map_add_item(cmd, "schema", schema);

    cbor_item_t *reply = send_tc_request(tc, cmd);
    cbor_decref(&cmd);
    return reply; /* caller owns this */
}

int hegel_send_new_collection(hegel_test_case *tc, size_t min_size, size_t max_size)
{
    /* Build: {"command": "new_collection", "min_size": min, "max_size": max} */
    cbor_item_t *cmd = cbor_new_definite_map(3);
    if (!cmd)
        return -1;
    cbor_map_add_string(cmd, "command", "new_collection");
    cbor_map_add_int(cmd, "min_size", (int64_t)min_size);
    cbor_map_add_int(cmd, "max_size", (int64_t)max_size);

    cbor_item_t *reply = send_tc_request(tc, cmd);
    cbor_decref(&cmd);
    if (!reply)
        return -1;

    /* Reply is an integer collection ID; unwrap any CBOR tags */
    const cbor_item_t *inner = cbor_unwrap_tags(reply);
    int collection_id = -1;
    if (inner && cbor_isa_uint(inner)) {
        collection_id = (int)cbor_get_uint_value(inner);
    } else if (inner && cbor_isa_negint(inner)) {
        collection_id = -(int)(cbor_get_uint_value(inner) + 1);
    }
    cbor_decref(&reply);
    return collection_id;
}

bool hegel_send_collection_more(hegel_test_case *tc, int collection_id)
{
    /* Build: {"command": "collection_more", "collection_id": id} */
    cbor_item_t *cmd = cbor_new_definite_map(2);
    if (!cmd)
        return false;
    cbor_map_add_string(cmd, "command", "collection_more");
    cbor_map_add_int(cmd, "collection_id", (int64_t)collection_id);

    cbor_item_t *reply = send_tc_request(tc, cmd);
    cbor_decref(&cmd);
    if (!reply)
        return false;

    /* Reply is a boolean; unwrap any CBOR tags */
    const cbor_item_t *inner = cbor_unwrap_tags(reply);
    bool result = false;
    if (inner && cbor_isa_float_ctrl(inner) && cbor_is_bool(inner)) {
        result = cbor_get_bool(inner);
    }
    cbor_decref(&reply);
    return result;
}

void hegel_send_collection_reject(hegel_test_case *tc, int collection_id, const char *why)
{
    /* Build: {"command": "collection_reject", "collection_id": id, "why": reason} */
    int n = why ? 3 : 2;
    cbor_item_t *cmd = cbor_new_definite_map(n);
    if (!cmd)
        return;
    cbor_map_add_string(cmd, "command", "collection_reject");
    cbor_map_add_int(cmd, "collection_id", (int64_t)collection_id);
    if (why)
        cbor_map_add_string(cmd, "why", why);

    cbor_item_t *reply = send_tc_request(tc, cmd);
    cbor_decref(&cmd);
    if (reply)
        cbor_decref(&reply);
}

/* ================================================================
 * Core draw logic
 * ================================================================ */

void *hegel_draw_internal(hegel_test_case *tc, hegel_generator *gen)
{
    if (!tc || !gen)
        return NULL;

    /* Check if this generator is basic */
    hegel_basic_gen *basic = NULL;
    if (gen->vtable.as_basic)
        basic = gen->vtable.as_basic(gen);

    if (basic) {
        /* Schema-based generation: send schema, apply transform */
        cbor_item_t *raw = hegel_send_generate(tc, basic->schema);
        if (!raw)
            return NULL;

        if (basic->transform) {
            void *result = basic->transform(raw, basic->transform_ctx);
            cbor_decref(&raw);
            return result;
        }

        /* No transform -- return the raw CBOR item.
         * Caller is responsible for interpreting / freeing it. */
        return raw;
    }

    /* Non-basic: use compositional draw */
    if (gen->vtable.draw)
        return gen->vtable.draw(gen, tc);

    return NULL;
}
