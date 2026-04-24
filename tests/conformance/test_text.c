/*
 * Conformance test: text generator.
 *
 * Params: {"min_size": <int>, "max_size": <int>,
 *          "codec": <string>, "min_codepoint": <int>, "max_codepoint": <int>,
 *          "categories": [<string>, ...], "exclude_categories": [<string>, ...],
 *          "include_characters": <string>, "exclude_characters": <string>}
 * Metrics: {"codepoints": [<int>, ...]}
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
static char *g_codec;
static uint32_t g_min_codepoint;
static uint32_t g_max_codepoint;
static const char **g_categories;
static const char **g_exclude_categories;
static char *g_include_characters;
static char *g_exclude_characters;

/*
 * Decode a single UTF-8 codepoint from a byte buffer with known length.
 * Advances *offset. Returns the codepoint value, or (uint32_t)-1 on error.
 */
static uint32_t utf8_decode_buf(const unsigned char *buf, size_t len, size_t *offset)
{
    if (*offset >= len)
        return (uint32_t)-1;

    unsigned char b0 = buf[*offset];
    uint32_t cp;
    int extra;

    if (b0 < 0x80) {
        cp = b0;
        extra = 0;
    } else if ((b0 & 0xE0) == 0xC0) {
        cp = b0 & 0x1F;
        extra = 1;
    } else if ((b0 & 0xF0) == 0xE0) {
        cp = b0 & 0x0F;
        extra = 2;
    } else if ((b0 & 0xF8) == 0xF0) {
        cp = b0 & 0x07;
        extra = 3;
    } else {
        (*offset)++;
        return (uint32_t)-1;
    }

    if (*offset + 1 + (size_t)extra > len) {
        (*offset)++;
        return (uint32_t)-1;
    }

    for (int i = 0; i < extra; i++) {
        unsigned char cont = buf[*offset + 1 + (size_t)i];
        if ((cont & 0xC0) != 0x80) {
            (*offset)++;
            return (uint32_t)-1;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }

    *offset += 1 + (size_t)extra;
    return cp;
}

/*
 * Extract raw bytes and length from a CBOR string item (handles tags and
 * bytestrings). Returns pointer to internal data and sets *out_len.
 */
static const unsigned char *cbor_string_bytes(const cbor_item_t *item, size_t *out_len)
{
    if (!item) {
        *out_len = 0;
        return NULL;
    }

    /* Unwrap CBOR tags */
    while (cbor_isa_tag(item))
        item = cbor_tag_item(item);

    if (cbor_isa_string(item)) {
        *out_len = cbor_string_length(item);
        return cbor_string_handle(item);
    }
    if (cbor_isa_bytestring(item)) {
        *out_len = cbor_bytestring_length(item);
        return cbor_bytestring_handle(item);
    }

    *out_len = 0;
    return NULL;
}

static void test_fn(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    size_t effective_max = g_max_size > 0 ? (size_t)g_max_size
                         : (g_min_size > 0 ? (size_t)(g_min_size + 100) : 1000);

    /* Use draw_raw to get the CBOR item directly, avoiding C string null termination */
    void *raw = hegel_draw_raw(tc,
        hegel_text_ex((size_t)g_min_size,
                      effective_max,
                      g_codec,
                      g_min_codepoint, g_max_codepoint,
                      g_categories, g_exclude_categories,
                      g_include_characters, g_exclude_characters));
    if (!raw) {
        fprintf(metrics_file, "{\"codepoints\": []}\n");
        return;
    }

    cbor_item_t *item = (cbor_item_t *)raw;
    size_t byte_len = 0;
    const unsigned char *bytes = cbor_string_bytes(item, &byte_len);

    /* Decode UTF-8 codepoints using the known byte length */
    fprintf(metrics_file, "{\"codepoints\": [");
    size_t offset = 0;
    bool first = true;
    while (offset < byte_len) {
        uint32_t cp = utf8_decode_buf(bytes, byte_len, &offset);
        if (cp == (uint32_t)-1)
            continue;
        if (!first)
            fprintf(metrics_file, ", ");
        fprintf(metrics_file, "%u", cp);
        first = false;
    }
    fprintf(metrics_file, "]}\n");
    cbor_decref(&item);
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
        g_max_size = 0; /* 0 means no max set */

    int64_t tmp;
    if (json_get_int(params, "min_codepoint", &tmp))
        g_min_codepoint = (uint32_t)tmp;
    if (json_get_int(params, "max_codepoint", &tmp))
        g_max_codepoint = (uint32_t)tmp;

    g_codec = json_get_string(params, "codec");
    g_include_characters = json_get_string(params, "include_characters");
    g_exclude_characters = json_get_string(params, "exclude_characters");

    /* Parse categories arrays (NULL-terminated) */
    size_t cat_count = 0;
    char **cats = json_get_string_array(params, "categories", &cat_count);
    if (cats) {
        g_categories = (const char **)realloc(cats, (cat_count + 1) * sizeof(char *));
        g_categories[cat_count] = NULL;
    }

    size_t excl_count = 0;
    char **excl_cats = json_get_string_array(params, "exclude_categories", &excl_count);
    if (excl_cats) {
        g_exclude_categories = (const char **)realloc(excl_cats, (excl_count + 1) * sizeof(char *));
        g_exclude_categories[excl_count] = NULL;
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

    /* Cleanup */
    if (g_categories) {
        for (size_t i = 0; g_categories[i]; i++)
            free((char *)g_categories[i]);
        free(g_categories);
    }
    if (g_exclude_categories) {
        for (size_t i = 0; g_exclude_categories[i]; i++)
            free((char *)g_exclude_categories[i]);
        free(g_exclude_categories);
    }
    free(g_codec);
    free(g_include_characters);
    free(g_exclude_characters);

    hegel_results_free(&r);
    hegel_session_free(s);
    return r.passed ? 0 : 1;
}
