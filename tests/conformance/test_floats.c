/*
 * Conformance test: floats generator.
 *
 * Params: {"min_value": <float>, "max_value": <float>,
 *          "allow_nan": <bool>, "allow_infinity": <bool>}
 * Metrics: {"value": <float>}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"
#include "json_helpers.h"

static FILE *metrics_file;
static double g_min_value;
static double g_max_value;
static bool g_allow_nan;
static bool g_allow_infinity;

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    double val = hegel_draw_float(tc,
        hegel_floats_ex(g_min_value, g_max_value,
                        false, false,
                        g_allow_nan, g_allow_infinity, 64));

    if (isnan(val)) {
        fprintf(metrics_file, "{\"value\": \"NaN\"}\n");
    } else if (isinf(val)) {
        fprintf(metrics_file, "{\"value\": \"%s\"}\n",
                val > 0 ? "Infinity" : "-Infinity");
    } else {
        fprintf(metrics_file, "{\"value\": %.17g}\n", val);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s '<json_params>'\n", argv[0]);
        return 1;
    }

    const char *params = argv[1];

    if (!json_get_number(params, "min_value", &g_min_value)) {
        g_min_value = -1e308;
    }
    if (!json_get_number(params, "max_value", &g_max_value)) {
        g_max_value = 1e308;
    }
    if (!json_get_bool(params, "allow_nan", &g_allow_nan)) {
        g_allow_nan = false;
    }
    if (!json_get_bool(params, "allow_infinity", &g_allow_infinity)) {
        g_allow_infinity = false;
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
