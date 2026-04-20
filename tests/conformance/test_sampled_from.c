/*
 * Conformance test: sampled_from generator.
 *
 * Params: {"options": ["a", "b", "c"]}
 * Metrics: {"value": "<string>"}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"
#include "json_helpers.h"

static FILE *metrics_file;
static const char **g_options;
static size_t g_count;

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    char *val = hegel_draw_string(tc,
        hegel_sampled_from_strings(g_options, g_count));
    if (val) {
        fprintf(metrics_file, "{\"value\": \"%s\"}\n", val);
        free(val);
    } else {
        fprintf(metrics_file, "{\"value\": null}\n");
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s '<json_params>'\n", argv[0]);
        return 1;
    }

    const char *params = argv[1];

    char **options = json_get_string_array(params, "options", &g_count);
    if (!options || g_count == 0) {
        fprintf(stderr, "Missing or empty options array\n");
        return 1;
    }
    g_options = (const char **)options;

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

    /* Clean up options */
    for (size_t i = 0; i < g_count; i++)
        free(options[i]);
    free(options);

    return r.passed ? 0 : 1;
}
