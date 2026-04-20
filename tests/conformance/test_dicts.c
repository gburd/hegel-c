/*
 * Conformance test: dicts generator.
 *
 * Params: {"min_size": <int>, "max_size": <int>}
 * Metrics: {"length": <dict_length>}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cbor.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"
#include "json_helpers.h"

static FILE *metrics_file;
static int64_t g_min_size;
static int64_t g_max_size;

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;

    hegel_generator *keys = hegel_text(1, 5);
    hegel_generator *vals = hegel_integers(0, 100);
    hegel_generator *dict_gen = hegel_dicts(keys, vals, (size_t)g_min_size, (size_t)g_max_size);

    void *raw = hegel_draw_raw(tc, dict_gen);
    if (!raw) {
        fprintf(metrics_file, "{\"length\": 0}\n");
        hegel_generator_free(dict_gen);
        return;
    }

    cbor_item_t *map = (cbor_item_t *)raw;
    size_t length = 0;
    if (cbor_isa_map(map))
        length = cbor_map_size(map);

    fprintf(metrics_file, "{\"length\": %zu}\n", length);

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

    if (!json_get_int(params, "min_size", &g_min_size)) {
        g_min_size = 0;
    }
    if (!json_get_int(params, "max_size", &g_max_size)) {
        g_max_size = 5;
    }

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
    hegel_results_free(&r);
    hegel_session_free(s);
    return r.passed ? 0 : 1;
}
