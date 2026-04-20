/*
 * Conformance test: integers generator.
 *
 * Params: {"min_value": <int>, "max_value": <int>}
 * Metrics: {"value": <int>}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"
#include "json_helpers.h"

static FILE *metrics_file;
static int64_t g_min_value;
static int64_t g_max_value;

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    int64_t val = hegel_draw_int(tc, hegel_integers(g_min_value, g_max_value));
    fprintf(metrics_file, "{\"value\": %" PRId64 "}\n", val);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s '<json_params>'\n", argv[0]);
        return 1;
    }

    const char *params = argv[1];

    if (!json_get_int(params, "min_value", &g_min_value)) {
        fprintf(stderr, "Missing min_value\n");
        return 1;
    }
    if (!json_get_int(params, "max_value", &g_max_value)) {
        fprintf(stderr, "Missing max_value\n");
        return 1;
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
