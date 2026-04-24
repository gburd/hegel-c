/*
 * Conformance test: dicts generator.
 *
 * Params: {"min_size": <int>, "max_size": <int>, "key_type": <string>,
 *          "min_key": <int>, "max_key": <int>,
 *          "min_value": <int>, "max_value": <int>}
 * Metrics: {"size": <int>, "min_key": <int|null>, "max_key": <int|null>,
 *           "min_value": <int|null>, "max_value": <int|null>}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <cbor.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"
#include "json_helpers.h"

static FILE *metrics_file;
static int64_t g_min_size;
static int64_t g_max_size;
static char *g_key_type;
static int64_t g_min_key;
static int64_t g_max_key;
static int64_t g_min_value;
static int64_t g_max_value;

static int64_t cbor_to_int64(cbor_item_t *item)
{
    uint64_t raw;
    switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:  raw = cbor_get_uint8(item); break;
    case CBOR_INT_16: raw = cbor_get_uint16(item); break;
    case CBOR_INT_32: raw = cbor_get_uint32(item); break;
    case CBOR_INT_64: raw = cbor_get_uint64(item); break;
    default: return 0;
    }
    if (cbor_isa_uint(item))
        return (int64_t)raw;
    if (cbor_isa_negint(item))
        return -1 - (int64_t)raw;
    return 0;
}

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;

    bool integer_keys = g_key_type && strcmp(g_key_type, "integer") == 0;

    hegel_generator *keys;
    if (integer_keys)
        keys = hegel_integers(g_min_key, g_max_key);
    else
        keys = hegel_text(1, 5);

    hegel_generator *vals = hegel_integers(g_min_value, g_max_value);
    hegel_generator *dict_gen = hegel_dicts(keys, vals, (size_t)g_min_size, (size_t)g_max_size);

    void *raw = hegel_draw_raw(tc, dict_gen);
    if (!raw) {
        fprintf(metrics_file, "{\"size\": 0, \"min_key\": null, \"max_key\": null, \"min_value\": null, \"max_value\": null}\n");
        hegel_generator_free(dict_gen);
        return;
    }

    cbor_item_t *map = (cbor_item_t *)raw;
    size_t size = 0;
    int64_t min_key_val = INT64_MAX;
    int64_t max_key_val = INT64_MIN;
    int64_t min_val = INT64_MAX;
    int64_t max_val = INT64_MIN;

    if (cbor_isa_map(map)) {
        size = cbor_map_size(map);
        struct cbor_pair *pairs = cbor_map_handle(map);
        for (size_t i = 0; i < size; i++) {
            int64_t v = cbor_to_int64(pairs[i].value);
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;

            if (integer_keys) {
                int64_t k = cbor_to_int64(pairs[i].key);
                if (k < min_key_val) min_key_val = k;
                if (k > max_key_val) max_key_val = k;
            }
        }
    }

    if (size == 0) {
        fprintf(metrics_file, "{\"size\": 0, \"min_key\": null, \"max_key\": null, \"min_value\": null, \"max_value\": null}\n");
    } else if (integer_keys) {
        fprintf(metrics_file, "{\"size\": %zu, \"min_key\": %" PRId64 ", \"max_key\": %" PRId64
                ", \"min_value\": %" PRId64 ", \"max_value\": %" PRId64 "}\n",
                size, min_key_val, max_key_val, min_val, max_val);
    } else {
        fprintf(metrics_file, "{\"size\": %zu, \"min_value\": %" PRId64 ", \"max_value\": %" PRId64 "}\n",
                size, min_val, max_val);
    }

    cbor_decref(&map);
    hegel_generator_free(dict_gen);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s '<json_params>'\n", argv[0]);
        return 1;
    }

    const char *params = argv[1];

    if (!json_get_int(params, "min_size", &g_min_size))
        g_min_size = 0;
    if (!json_get_int(params, "max_size", &g_max_size))
        g_max_size = g_min_size + 50; /* null means no upper bound */

    g_key_type = json_get_string(params, "key_type");

    if (!json_get_int(params, "min_key", &g_min_key))
        g_min_key = -1000;
    if (!json_get_int(params, "max_key", &g_max_key))
        g_max_key = 1000;
    if (!json_get_int(params, "min_value", &g_min_value))
        g_min_value = -1000;
    if (!json_get_int(params, "max_value", &g_max_value))
        g_max_value = 1000;

    const char *tc_str = getenv("CONFORMANCE_TEST_CASES");
    int test_cases = tc_str ? atoi(tc_str) : 50;

    const char *metrics_path = getenv("CONFORMANCE_METRICS_FILE");
    if (!metrics_path) {
        fprintf(stderr, "CONFORMANCE_METRICS_FILE not set\n");
        return 1;
    }
    metrics_file = fopen(metrics_path, "w");
    if (!metrics_file) {
        fprintf(stderr, "Cannot open metrics file: %s\n", metrics_path);
        return 1;
    }

    hegel_session *s = hegel_session_new();
    if (!s) {
        fprintf(stderr, "Failed to create session\n");
        fclose(metrics_file);
        return 1;
    }

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = test_cases;

    hegel_results r = hegel_run_test(s, test_fn, NULL, &settings);

    fclose(metrics_file);
    free(g_key_type);
    hegel_results_free(&r);
    hegel_session_free(s);
    return r.passed ? 0 : 1;
}
