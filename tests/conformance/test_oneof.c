/*
 * Conformance test: one_of generator.
 *
 * Params: {"ranges": [{"min_value": <int>, "max_value": <int>}, ...],
 *          "mode": "basic"|"map_negate"|"filter_even"}
 * Metrics: {"value": <int>}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <cbor.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"
#include "json_helpers.h"

#define MAX_BRANCHES 16

static FILE *metrics_file;
static size_t g_n_branches;
static int64_t g_min_values[MAX_BRANCHES];
static int64_t g_max_values[MAX_BRANCHES];

typedef enum {
    MODE_BASIC,
    MODE_MAP_NEGATE,
    MODE_FILTER_EVEN
} oneof_mode;

static oneof_mode g_mode;

/* Read the raw uint payload from a CBOR integer item, regardless of width. */
static uint64_t cbor_get_uint_val(const cbor_item_t *item)
{
    switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:  return cbor_get_uint8(item);
    case CBOR_INT_16: return cbor_get_uint16(item);
    case CBOR_INT_32: return cbor_get_uint32(item);
    case CBOR_INT_64: return cbor_get_uint64(item);
    }
    return 0;
}

/* Read a signed int64 from a CBOR uint or negint item. */
static int64_t cbor_to_int64(const cbor_item_t *item)
{
    if (cbor_isa_negint(item))
        return -1 - (int64_t)cbor_get_uint_val(item);
    return (int64_t)cbor_get_uint_val(item);
}

/* Build a CBOR integer item from a signed int64. */
static cbor_item_t *int64_to_cbor(int64_t value)
{
    if (value >= 0)
        return cbor_build_uint64((uint64_t)value);
    cbor_item_t *v = cbor_new_int64();
    if (!v)
        return NULL;
    cbor_set_uint64(v, (uint64_t)(-(value + 1)));
    cbor_mark_negint(v);
    return v;
}

static void *negate_int_fn(void *value, void *ctx)
{
    (void)ctx;
    cbor_item_t *item = (cbor_item_t *)value;
    int64_t v = cbor_to_int64(item);
    cbor_decref(&item);
    return int64_to_cbor(-v);
}

static bool is_even_fn(void *value, void *ctx)
{
    (void)ctx;
    cbor_item_t *item = (cbor_item_t *)value;
    int64_t v = cbor_to_int64(item);
    return (v % 2) == 0;
}

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;

    hegel_generator *branches[MAX_BRANCHES];
    for (size_t i = 0; i < g_n_branches; i++) {
        hegel_generator *gen = hegel_integers(g_min_values[i], g_max_values[i]);
        if (g_mode == MODE_MAP_NEGATE) {
            gen = hegel_map(gen, negate_int_fn, NULL, NULL);
        } else if (g_mode == MODE_FILTER_EVEN) {
            gen = hegel_filter(gen, is_even_fn, NULL);
        }
        branches[i] = gen;
    }

    hegel_generator *combined = hegel_one_of(branches, g_n_branches);
    void *raw = hegel_draw_raw(tc, combined);
    if (!raw) {
        fprintf(metrics_file, "{\"value\": 0}\n");
        hegel_generator_free(combined);
        return;
    }

    cbor_item_t *item = (cbor_item_t *)raw;
    int64_t val = cbor_to_int64(item);

    fprintf(metrics_file, "{\"value\": %" PRId64 "}\n", val);

    cbor_decref(&item);
    hegel_generator_free(combined);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s '<json_params>'\n", argv[0]);
        return 1;
    }

    const char *params = argv[1];

    /* Parse mode */
    char *mode_str = json_get_string(params, "mode");
    if (!mode_str || strcmp(mode_str, "basic") == 0) {
        g_mode = MODE_BASIC;
    } else if (strcmp(mode_str, "map_negate") == 0) {
        g_mode = MODE_MAP_NEGATE;
    } else if (strcmp(mode_str, "filter_even") == 0) {
        g_mode = MODE_FILTER_EVEN;
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode_str);
        free(mode_str);
        return 1;
    }
    free(mode_str);

    /* Parse ranges array */
    size_t n_ranges = 0;
    char **ranges = json_get_object_array(params, "ranges", &n_ranges);
    if (!ranges || n_ranges == 0) {
        fprintf(stderr, "Missing or empty ranges array\n");
        return 1;
    }
    if (n_ranges > MAX_BRANCHES) {
        fprintf(stderr, "Too many branches (max %d)\n", MAX_BRANCHES);
        for (size_t i = 0; i < n_ranges; i++)
            free(ranges[i]);
        free(ranges);
        return 1;
    }

    g_n_branches = n_ranges;
    for (size_t i = 0; i < n_ranges; i++) {
        if (!json_get_int(ranges[i], "min_value", &g_min_values[i])) {
            fprintf(stderr, "Missing min_value in range %zu\n", i);
            for (size_t j = 0; j < n_ranges; j++)
                free(ranges[j]);
            free(ranges);
            return 1;
        }
        if (!json_get_int(ranges[i], "max_value", &g_max_values[i])) {
            fprintf(stderr, "Missing max_value in range %zu\n", i);
            for (size_t j = 0; j < n_ranges; j++)
                free(ranges[j]);
            free(ranges);
            return 1;
        }
    }

    for (size_t i = 0; i < n_ranges; i++)
        free(ranges[i]);
    free(ranges);

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
