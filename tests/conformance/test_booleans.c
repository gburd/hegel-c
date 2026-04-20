/*
 * Conformance test: booleans generator.
 *
 * Params: (none)
 * Metrics: {"value": true|false}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"

static FILE *metrics_file;

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    bool val = hegel_draw_bool(tc, hegel_booleans());
    fprintf(metrics_file, "{\"value\": %s}\n", val ? "true" : "false");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

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
