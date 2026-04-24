#include "hegel/hegel.h"
#include "hegel/protocol.h"
#include "runner.h"
#include "test_case.h"
#include "protocol/cbor_helpers.h"
#include "protocol/connection.h"

#include <cbor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Send mark_complete on a test case stream.
 */
static int send_mark_complete(hegel_stream *stream, hegel_status status,
                               const char *error_msg)
{
    const char *status_str;
    switch (status) {
    case HEGEL_STATUS_VALID:
        status_str = "VALID";
        break;
    case HEGEL_STATUS_INVALID:
        status_str = "INVALID";
        break;
    case HEGEL_STATUS_INTERESTING:
        status_str = "INTERESTING";
        break;
    default:
        status_str = "VALID";
        break;
    }

    int pairs = 2;
    if (status == HEGEL_STATUS_INTERESTING && error_msg)
        pairs = 3;

    cbor_item_t *map = cbor_new_definite_map(pairs);
    if (!map)
        return HEGEL_ERR_ALLOC;

    cbor_map_add_string(map, "command", "mark_complete");
    cbor_map_add_string(map, "status", status_str);

    if (status == HEGEL_STATUS_INTERESTING && error_msg)
        cbor_map_add_string(map, "error", error_msg);

    cbor_item_t *reply = hegel_stream_request(stream, map);
    cbor_decref(&map);
    if (reply)
        cbor_decref(&reply);

    return HEGEL_OK;
}

/*
 * Build the run_test CBOR request.
 * stream_id is the client-created stream where test events will be received.
 */
static cbor_item_t *build_run_test_request(const hegel_settings *settings,
                                            uint32_t stream_id)
{
    /* Count pairs: command + test_cases + stream_id is always present */
    int pairs = 3;
    if (settings->seed != 0)
        pairs++;
    if (settings->derandomize)
        pairs++;
    if (settings->database != NULL)
        pairs++;
    if (settings->database_key != NULL)
        pairs++;

    cbor_item_t *map = cbor_new_definite_map(pairs);
    if (!map)
        return NULL;

    cbor_map_add_string(map, "command", "run_test");
    cbor_map_add_int(map, "test_cases", settings->max_examples);
    cbor_map_add_uint(map, "stream_id", stream_id);

    if (settings->seed != 0)
        cbor_map_add_int(map, "seed", (int64_t)settings->seed);

    if (settings->derandomize)
        cbor_map_add_bool(map, "derandomize", true);

    if (settings->database != NULL)
        cbor_map_add_string(map, "database", settings->database);

    if (settings->database_key != NULL)
        cbor_map_add_string(map, "database_key", settings->database_key);

    return map;
}

/*
 * Parse the "test_done" event into hegel_results.
 * The event has shape: {"event": "test_done", "results": { "passed": bool, ... }}
 */
static hegel_results parse_test_done(cbor_item_t *event)
{
    hegel_results results;
    memset(&results, 0, sizeof(results));

    /* Results are nested under the "results" key */
    cbor_item_t *r = cbor_map_get(event, "results");
    if (!r || !cbor_isa_map(r))
        return results;

    cbor_item_t *passed = cbor_map_get(r, "passed");
    if (passed && cbor_isa_float_ctrl(passed) && cbor_is_bool(passed))
        results.passed = cbor_get_bool(passed);

    cbor_item_t *interesting = cbor_map_get(r, "interesting_test_cases");
    if (interesting && cbor_isa_uint(interesting))
        results.interesting_test_cases = (int)cbor_get_int_value(interesting);

    cbor_item_t *valid = cbor_map_get(r, "valid_test_cases");
    if (valid && cbor_isa_uint(valid))
        results.valid_test_cases = (int)cbor_get_int_value(valid);

    cbor_item_t *invalid = cbor_map_get(r, "invalid_test_cases");
    if (invalid && cbor_isa_uint(invalid))
        results.invalid_test_cases = (int)cbor_get_int_value(invalid);

    cbor_item_t *total = cbor_map_get(r, "test_cases");
    if (total && cbor_isa_uint(total))
        results.total_test_cases = (int)cbor_get_int_value(total);

    cbor_item_t *seed_item = cbor_map_get(r, "seed");
    if (seed_item)
        results.seed = cbor_get_string(seed_item);

    cbor_item_t *error_item = cbor_map_get(r, "error");
    if (error_item)
        results.error = cbor_get_string(error_item);

    return results;
}

hegel_results hegel_run_test(hegel_session *s, hegel_test_fn fn, void *user_data,
                              const hegel_settings *settings)
{
    hegel_results fail_results;
    memset(&fail_results, 0, sizeof(fail_results));
    fail_results.passed = false;

    if (!s || !s->conn || !fn) {
        fail_results.error = strdup("invalid session or test function");
        return fail_results;
    }

    /* Use default settings if none provided */
    hegel_settings default_settings = {.max_examples = 100};
    if (!settings)
        settings = &default_settings;

    /* Get the control stream */
    hegel_stream *control = hegel_connection_control_stream(s->conn);
    if (!control) {
        fail_results.error = strdup("failed to get control stream");
        return fail_results;
    }

    /* Create a client stream for receiving test events */
    hegel_stream *test_stream = hegel_connection_new_stream(s->conn);
    if (!test_stream) {
        fail_results.error = strdup("failed to create test stream");
        return fail_results;
    }

    /* Build and send run_test request on control stream */
    cbor_item_t *request = build_run_test_request(settings,
                                                   hegel_stream_id(test_stream));
    if (!request) {
        hegel_stream_free(test_stream);
        fail_results.error = strdup("failed to build run_test request");
        return fail_results;
    }

    cbor_item_t *run_reply = hegel_stream_request(control, request);
    cbor_decref(&request);
    if (!run_reply) {
        hegel_stream_free(test_stream);
        fail_results.error = strdup("run_test request failed");
        return fail_results;
    }
    cbor_decref(&run_reply);

    /* Event loop: receive events on the test stream */
    for (;;) {
        uint32_t msg_id;
        cbor_item_t *event = hegel_stream_recv_event(test_stream, &msg_id);
        if (!event) {
            hegel_stream_free(test_stream);
            fail_results.error = strdup("failed to receive event from server");
            return fail_results;
        }

        /* Determine event type */
        cbor_item_t *event_type = cbor_map_get(event, "event");
        if (!event_type) {
            cbor_decref(&event);
            hegel_stream_free(test_stream);
            fail_results.error = strdup("received event without 'event' key");
            return fail_results;
        }

        char *type_str = cbor_get_string(event_type);
        if (!type_str) {
            cbor_decref(&event);
            hegel_stream_free(test_stream);
            fail_results.error = strdup("event type is not a string");
            return fail_results;
        }

        if (strcmp(type_str, "test_case") == 0) {
            /* Extract stream_id and is_final */
            cbor_item_t *sid_item = cbor_map_get(event, "stream_id");
            cbor_item_t *final_item = cbor_map_get(event, "is_final");

            volatile uint32_t tc_stream_id = 0;
            volatile bool is_final = false;

            if (sid_item && cbor_isa_uint(sid_item))
                tc_stream_id = (uint32_t)cbor_get_int_value(sid_item);
            if (final_item && cbor_isa_float_ctrl(final_item) && cbor_is_bool(final_item))
                is_final = cbor_get_bool(final_item);

            free(type_str);
            cbor_decref(&event);

            /* Acknowledge the test_case event */
            cbor_item_t *ack = cbor_new_definite_map(1);
            if (ack) {
                cbor_item_t *null_val = cbor_new_null();
                if (null_val)
                    cbor_map_add_item(ack, "result", null_val);
                hegel_stream_reply_event(test_stream, msg_id, ack);
                cbor_decref(&ack);
                if (null_val)
                    cbor_decref(&null_val);
            }

            /* Connect to the test case stream */
            hegel_stream *tc_stream = hegel_connection_connect_stream(s->conn, tc_stream_id);
            if (!tc_stream)
                continue;

            /* Set up test case context */
            hegel_test_case tc;
            memset(&tc, 0, sizeof(tc));
            tc.stream = tc_stream;
            tc.is_final = is_final;
            tc.aborted = false;
            tc.status = HEGEL_STATUS_VALID;
            tc.jmp_reason = 0;
            tc.error_message = NULL;

            /* Set thread-local current test case */
            hegel_set_current_test_case(&tc);

            /* Set up longjmp target */
            int jmp_val = setjmp(tc.escape_jmp);
            if (jmp_val == 0) {
                /* Normal execution: call the user's test function */
                fn(&tc, user_data);

                /* If we get here, test passed normally -> VALID */
                send_mark_complete(tc_stream, tc.status, NULL);
            } else if (jmp_val == HEGEL_JMP_ASSUME) {
                /* hegel_assume(false) was called -> INVALID */
                send_mark_complete(tc_stream, HEGEL_STATUS_INVALID, NULL);
            } else if (jmp_val == HEGEL_JMP_STOP_TEST) {
                /* StopTest: do NOT send mark_complete */
            }

            /* Clear thread-local */
            hegel_set_current_test_case(NULL);

            /* Clean up error message if any */
            free(tc.error_message);

            /* Unregister and free the test case stream */
            hegel_connection_unregister_stream(s->conn, tc_stream_id);
            hegel_stream_free(tc_stream);

        } else if (strcmp(type_str, "test_done") == 0) {
            /* Parse results */
            hegel_results results = parse_test_done(event);

            free(type_str);

            /* Reply to acknowledge */
            cbor_item_t *done_reply = cbor_new_definite_map(1);
            if (done_reply) {
                cbor_map_add_bool(done_reply, "result", true);
                hegel_stream_reply_event(test_stream, msg_id, done_reply);
                cbor_decref(&done_reply);
            }

            cbor_decref(&event);
            return results;
        } else {
            /* Unknown event type -- reply and continue */
            free(type_str);

            cbor_item_t *unknown_reply = cbor_new_definite_map(1);
            if (unknown_reply) {
                cbor_item_t *null_val = cbor_new_null();
                if (null_val)
                    cbor_map_add_item(unknown_reply, "result", null_val);
                hegel_stream_reply_event(test_stream, msg_id, unknown_reply);
                cbor_decref(&unknown_reply);
                if (null_val)
                    cbor_decref(&null_val);
            }

            cbor_decref(&event);
        }
    }
}

void hegel_results_free(hegel_results *r)
{
    if (!r)
        return;
    free(r->seed);
    r->seed = NULL;
    free(r->error);
    r->error = NULL;
}
