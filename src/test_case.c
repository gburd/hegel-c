#include "test_case.h"
#include "generators/generator.h"
#include "protocol/cbor_helpers.h"
#include "protocol/stream.h"

#include <cbor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Thread-local pointer to the currently executing test case.
 * This allows hegel_assume() / hegel_target() / hegel_note() to work
 * without requiring an explicit test_case parameter.
 */
static __thread hegel_test_case *_current_tc = NULL;

hegel_test_case *hegel_current_test_case(void)
{
    return _current_tc;
}

void hegel_set_current_test_case(hegel_test_case *tc)
{
    _current_tc = tc;
}

void hegel_assume(bool condition)
{
    if (condition)
        return;

    hegel_test_case *tc = _current_tc;
    if (!tc) {
        fprintf(stderr, "hegel warning: hegel_assume() called with no active test case\n");
        return;
    }

    tc->status = HEGEL_STATUS_INVALID;
    tc->jmp_reason = HEGEL_JMP_ASSUME;
    longjmp(tc->escape_jmp, HEGEL_JMP_ASSUME);
}

void hegel_fail(const char *message)
{
    hegel_test_case *tc = _current_tc;
    if (!tc) {
        fprintf(stderr, "hegel warning: hegel_fail() called with no active test case\n");
        return;
    }

    tc->status = HEGEL_STATUS_INTERESTING;
    tc->jmp_reason = HEGEL_JMP_FAIL;
    free(tc->error_message);
    tc->error_message = message ? strdup(message) : strdup("test failed");
    longjmp(tc->escape_jmp, HEGEL_JMP_FAIL);
}

void hegel_target(double value, const char *label)
{
    hegel_test_case *tc = _current_tc;
    if (!tc || !tc->stream) {
        fprintf(stderr, "hegel warning: hegel_target() called with no active test case\n");
        return;
    }

    /* Build: {"command": "target", "value": <float>, "label": <string or null>} */
    int pairs = (label != NULL) ? 3 : 2;
    cbor_item_t *map = cbor_new_definite_map(pairs);
    if (!map)
        return;

    cbor_map_add_string(map, "command", "target");

    /* Add float value */
    cbor_item_t *key = cbor_build_string("value");
    cbor_item_t *val = cbor_build_float8(value);
    if (key && val) {
        struct cbor_pair pair = {.key = cbor_move(key), .value = cbor_move(val)};
        cbor_map_add(map, pair);
    } else {
        if (key) cbor_decref(&key);
        if (val) cbor_decref(&val);
        cbor_decref(&map);
        return;
    }

    if (label != NULL)
        cbor_map_add_string(map, "label", label);

    /* Send as a request on the test case stream; ignore the reply */
    cbor_item_t *reply = hegel_stream_request(tc->stream, map);
    cbor_decref(&map);
    if (reply)
        cbor_decref(&reply);
}

void hegel_note(const char *message)
{
    hegel_test_case *tc = _current_tc;
    if (!tc) {
        fprintf(stderr, "hegel warning: hegel_note() called with no active test case\n");
        return;
    }
    if (!message)
        return;

    /* Only print notes during the final (shrunk) replay */
    if (tc->is_final)
        fprintf(stderr, "%s\n", message);
}

/*
 * Generic draw: delegates to the generator system's hegel_draw_internal,
 * which handles both basic (schema-based) and non-basic (compositional)
 * generation.
 *
 * Returns a cbor_item_t* owned by the caller, or NULL on error.
 */
void *hegel_draw_raw(hegel_test_case *tc, hegel_generator *gen)
{
    if (!tc || !gen || !tc->stream)
        return NULL;

    return hegel_draw_internal(tc, gen);
}

/*
 * Typed draw wrappers.
 * Each calls hegel_draw_raw and extracts the appropriate C type.
 */

int64_t hegel_draw_int(hegel_test_case *tc, hegel_generator *gen)
{
    cbor_item_t *item = hegel_draw_raw(tc, gen);
    if (!item)
        return 0;

    /* Unwrap any CBOR tags */
    const cbor_item_t *inner = cbor_unwrap_tags(item);
    int64_t result = 0;
    if (inner && cbor_isa_uint(inner)) {
        result = (int64_t)cbor_get_uint_value(inner);
    } else if (inner && cbor_isa_negint(inner)) {
        result = -1 - (int64_t)cbor_get_uint_value(inner);
    }
    cbor_decref(&item);
    return result;
}

double hegel_draw_float(hegel_test_case *tc, hegel_generator *gen)
{
    cbor_item_t *item = hegel_draw_raw(tc, gen);
    if (!item)
        return 0.0;

    /* Unwrap any CBOR tags */
    const cbor_item_t *inner = cbor_unwrap_tags(item);
    double result = 0.0;
    if (inner && cbor_isa_float_ctrl(inner) && cbor_is_float(inner)) {
        /* Must check width: NaN/Infinity are often encoded as float16 */
        switch (cbor_float_get_width(inner)) {
        case CBOR_FLOAT_16:
            result = cbor_float_get_float2(inner);
            break;
        case CBOR_FLOAT_32:
            result = cbor_float_get_float4(inner);
            break;
        case CBOR_FLOAT_64:
            result = cbor_float_get_float8(inner);
            break;
        default:
            break;
        }
    } else if (inner && cbor_isa_uint(inner)) {
        /* The server may encode whole-number floats (e.g. 0.0, 1.0) as CBOR
         * integers rather than CBOR floats. This is valid per the CBOR spec
         * (RFC 8949 preferred serialization) and happens in practice during
         * generation and shrinking. */
        result = (double)cbor_get_uint_value(inner);
    } else if (inner && cbor_isa_negint(inner)) {
        result = (double)(-1 - (int64_t)cbor_get_uint_value(inner));
    }
    cbor_decref(&item);
    return result;
}

bool hegel_draw_bool(hegel_test_case *tc, hegel_generator *gen)
{
    cbor_item_t *item = hegel_draw_raw(tc, gen);
    if (!item)
        return false;

    /* Unwrap any CBOR tags */
    const cbor_item_t *inner = cbor_unwrap_tags(item);
    bool result = false;
    if (inner && cbor_isa_float_ctrl(inner) && cbor_is_bool(inner)) {
        result = cbor_get_bool(inner);
    }
    cbor_decref(&item);
    return result;
}

char *hegel_draw_string(hegel_test_case *tc, hegel_generator *gen)
{
    cbor_item_t *item = hegel_draw_raw(tc, gen);
    if (!item)
        return NULL;

    char *result = cbor_get_string(item);
    cbor_decref(&item);
    return result;
}

uint8_t *hegel_draw_bytes(hegel_test_case *tc, hegel_generator *gen, size_t *out_len)
{
    cbor_item_t *item = hegel_draw_raw(tc, gen);
    if (!item) {
        if (out_len)
            *out_len = 0;
        return NULL;
    }

    /* Unwrap any CBOR tags */
    const cbor_item_t *inner = cbor_unwrap_tags(item);
    uint8_t *result = NULL;
    size_t len = 0;

    if (inner && cbor_isa_bytestring(inner)) {
        len = cbor_bytestring_length(inner);
        if (len > 0) {
            result = malloc(len);
            if (result)
                memcpy(result, cbor_bytestring_handle(inner), len);
            else
                len = 0;
        }
    }

    if (out_len)
        *out_len = len;

    cbor_decref(&item);
    return result;
}
