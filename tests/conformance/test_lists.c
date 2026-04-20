/*
 * Conformance test: lists generator.
 *
 * Params: {"min_size": <int>, "max_size": <int>,
 *          "element_min": <int>, "element_max": <int>}
 * Metrics: {"length": <list_length>}
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
static int64_t g_element_min;
static int64_t g_element_max;

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;

    hegel_generator *elem_gen = hegel_integers(g_element_min, g_element_max);
    hegel_generator *list_gen = hegel_lists(elem_gen, (size_t)g_min_size, (size_t)g_max_size);

    void *raw = hegel_draw_raw(tc, list_gen);
    if (!raw) {
        fprintf(metrics_file, "{\"length\": 0}\n");
        hegel_generator_free(list_gen);
        return;
    }

    cbor_item_t *arr = (cbor_item_t *)raw;
    size_t length = 0;
    if (cbor_isa_array(arr))
        length = cbor_array_size(arr);

    fprintf(metrics_file, "{\"length\": %zu}\n", length);

    cbor_decref(&arr);
    hegel_generator_free(list_gen);
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
        g_max_size = 10;
    }
    if (!json_get_int(params, "element_min", &g_element_min)) {
        g_element_min = 0;
    }
    if (!json_get_int(params, "element_max", &g_element_max)) {
        g_element_max = 100;
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
