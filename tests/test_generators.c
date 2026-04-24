/*
 * Unit tests for generator schema construction.
 *
 * These tests verify that generators produce correct CBOR schemas
 * without requiring a running server.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdlib.h>
#include <string.h>
#include <cbor.h>

#include "hegel/generators.h"
#include "generators/generator.h"
#include "protocol/cbor_helpers.h"

/* ------------------------------------------------------------------
 * Helper: look up a string value from a CBOR map by key.
 * Returns a newly allocated string or NULL.
 * ------------------------------------------------------------------ */
static char *get_map_string(cbor_item_t *map, const char *key)
{
    cbor_item_t *val = cbor_map_get(map, key);
    if (!val)
        return NULL;
    return cbor_get_string(val);
}

/* Look up an int64 value. Returns 0 if not found. */
static int64_t get_map_int(cbor_item_t *map, const char *key)
{
    cbor_item_t *val = cbor_map_get(map, key);
    if (!val)
        return 0;
    if (cbor_isa_uint(val))
        return (int64_t)cbor_get_uint_value(val);
    if (cbor_isa_negint(val))
        return -(int64_t)(cbor_get_uint_value(val) + 1);
    return 0;
}

/* ------------------------------------------------------------------
 * Test: integers schema
 * ------------------------------------------------------------------ */
static void test_integers_schema(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_integers(0, 100);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);
    assert_non_null(basic->schema);

    /* Schema: {"type":"integer","min_value":0,"max_value":100} */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "integer");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_value"), 0);
    assert_int_equal(get_map_int(basic->schema, "max_value"), 100);

    /* No transform for raw integer generators */
    assert_null(basic->transform);

    hegel_generator_free(gen);
}

/* Test integers with negative range. */
static void test_integers_negative(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_integers(-50, -10);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    char *type = get_map_string(basic->schema, "type");
    assert_string_equal(type, "integer");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_value"), -50);
    assert_int_equal(get_map_int(basic->schema, "max_value"), -10);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: booleans schema
 * ------------------------------------------------------------------ */
static void test_booleans_schema(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_booleans();
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Schema: {"type":"boolean"} */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "boolean");
    free(type);

    assert_null(basic->transform);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: text schema
 * ------------------------------------------------------------------ */
static void test_text_schema(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_text(1, 10);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Schema: {"type":"string","min_size":1,"max_size":10} */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "string");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_size"), 1);
    assert_int_equal(get_map_int(basic->schema, "max_size"), 10);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: binary schema
 * ------------------------------------------------------------------ */
static void test_binary_schema(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_binary(0, 256);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "binary");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_size"), 0);
    assert_int_equal(get_map_int(basic->schema, "max_size"), 256);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: nulls schema
 * ------------------------------------------------------------------ */
static void test_nulls_schema(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_nulls();
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Schema: {"type": "null"} */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "null");
    free(type);

    /* No transform for nulls generator */
    assert_null(basic->transform);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: arrays alias (delegates to lists)
 * ------------------------------------------------------------------ */
static void test_arrays_alias(void **state)
{
    (void)state;

    hegel_generator *elems = hegel_integers(0, 50);
    assert_non_null(elems);

    hegel_generator *gen = hegel_arrays(elems, 2, 8);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Should produce the same schema as hegel_lists */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "list");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_size"), 2);
    assert_int_equal(get_map_int(basic->schema, "max_size"), 8);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: just_null schema
 * ------------------------------------------------------------------ */
static void test_just_null_schema(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_just_null();
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Schema: {"constant": null} */
    cbor_item_t *val = cbor_map_get(basic->schema, "constant");
    assert_non_null(val);
    assert_true(cbor_is_null(val));

    /* Has a transform (returns NULL constant) */
    assert_non_null(basic->transform);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: sampled_from_strings schema
 * ------------------------------------------------------------------ */
static void test_sampled_from_strings_schema(void **state)
{
    (void)state;

    const char *options[] = {"alpha", "beta", "gamma"};
    hegel_generator *gen = hegel_sampled_from_strings(options, 3);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Schema should be an integer schema [0, count-1] */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "integer");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_value"), 0);
    assert_int_equal(get_map_int(basic->schema, "max_value"), 2);

    /* Has a transform (converts index to string) */
    assert_non_null(basic->transform);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: sampled_from_floats schema
 * ------------------------------------------------------------------ */
static void test_sampled_from_floats_schema(void **state)
{
    (void)state;

    const double options[] = {1.5, 2.7, 3.14};
    hegel_generator *gen = hegel_sampled_from_floats(options, 3);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Schema should be an integer schema [0, count-1] */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "integer");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_value"), 0);
    assert_int_equal(get_map_int(basic->schema, "max_value"), 2);

    /* Has a transform (converts index to float) */
    assert_non_null(basic->transform);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: sampled_from_bools schema
 * ------------------------------------------------------------------ */
static void test_sampled_from_bools_schema(void **state)
{
    (void)state;

    const bool options[] = {true, false};
    hegel_generator *gen = hegel_sampled_from_bools(options, 2);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    /* Schema should be an integer schema [0, count-1] */
    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "integer");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_value"), 0);
    assert_int_equal(get_map_int(basic->schema, "max_value"), 1);

    /* Has a transform (converts index to bool) */
    assert_non_null(basic->transform);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: map preserves basic-ness (central optimization)
 * ------------------------------------------------------------------ */
static void *double_int(void *value, void *ctx)
{
    (void)ctx;
    /* value is a cbor_item_t* from the basic generator */
    cbor_item_t *item = (cbor_item_t *)value;
    int64_t v = 0;
    if (cbor_isa_uint(item))
        v = (int64_t)cbor_get_uint_value(item);
    int64_t *result = malloc(sizeof(int64_t));
    if (result)
        *result = v * 2;
    return result;
}

static void test_map_preserves_basic(void **state)
{
    (void)state;

    hegel_generator *source = hegel_integers(0, 50);
    assert_non_null(source);

    /* Source is basic */
    hegel_basic_gen *src_basic = source->vtable.as_basic(source);
    assert_non_null(src_basic);

    hegel_generator *mapped = hegel_map(source, double_int, NULL, free);
    assert_non_null(mapped);

    /* Mapped generator should STILL be basic */
    hegel_basic_gen *map_basic = mapped->vtable.as_basic(mapped);
    assert_non_null(map_basic);

    /* Schema should be the same as the source's */
    char *type = get_map_string(map_basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "integer");
    free(type);

    assert_int_equal(get_map_int(map_basic->schema, "min_value"), 0);
    assert_int_equal(get_map_int(map_basic->schema, "max_value"), 50);

    /* Has a composed transform */
    assert_non_null(map_basic->transform);

    hegel_generator_free(mapped);
}

/* ------------------------------------------------------------------
 * Test: filter becomes non-basic
 * ------------------------------------------------------------------ */
static bool always_true(void *value, void *ctx)
{
    (void)value;
    (void)ctx;
    return true;
}

static void test_filter_becomes_non_basic(void **state)
{
    (void)state;

    hegel_generator *source = hegel_integers(0, 100);
    assert_non_null(source);

    /* Source is basic */
    assert_non_null(source->vtable.as_basic(source));

    hegel_generator *filtered = hegel_filter(source, always_true, NULL);
    assert_non_null(filtered);

    /* Filtered generator should NOT be basic */
    hegel_basic_gen *filt_basic = filtered->vtable.as_basic(filtered);
    assert_null(filt_basic);

    hegel_generator_free(filtered);
}

/* ------------------------------------------------------------------
 * Test: flat_map is always non-basic
 * ------------------------------------------------------------------ */
static hegel_generator *flatmap_fn(void *value, void *ctx)
{
    (void)value;
    (void)ctx;
    return hegel_integers(0, 10);
}

static void test_flat_map_non_basic(void **state)
{
    (void)state;

    hegel_generator *source = hegel_integers(0, 100);
    assert_non_null(source);

    hegel_generator *fm = hegel_flat_map(source, flatmap_fn, NULL);
    assert_non_null(fm);

    hegel_basic_gen *fm_basic = fm->vtable.as_basic(fm);
    assert_null(fm_basic);

    hegel_generator_free(fm);
}

/* ------------------------------------------------------------------
 * Test: one_of with all identity-transform basic generators
 * ------------------------------------------------------------------ */
static void test_one_of_all_basic(void **state)
{
    (void)state;

    hegel_generator *g1 = hegel_integers(0, 10);
    hegel_generator *g2 = hegel_integers(20, 30);
    assert_non_null(g1);
    assert_non_null(g2);

    hegel_generator *gens[] = {g1, g2};
    hegel_generator *combined = hegel_one_of(gens, 2);
    assert_non_null(combined);

    /* All basic + identity = basic one_of */
    hegel_basic_gen *basic = combined->vtable.as_basic(combined);
    assert_non_null(basic);

    /* Schema should have a "one_of" key */
    cbor_item_t *one_of_arr = cbor_map_get(basic->schema, "one_of");
    assert_non_null(one_of_arr);
    assert_true(cbor_isa_array(one_of_arr));
    assert_int_equal(cbor_array_size(one_of_arr), 2);

    /* No transform for Case 1 */
    assert_null(basic->transform);

    hegel_generator_free(combined);
}

/* ------------------------------------------------------------------
 * Test: floats schema
 * ------------------------------------------------------------------ */
static void test_floats_schema(void **state)
{
    (void)state;

    hegel_generator *gen = hegel_floats(0.0, 1.0);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "float");
    free(type);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: lists schema (basic elements)
 * ------------------------------------------------------------------ */
static void test_lists_schema(void **state)
{
    (void)state;

    hegel_generator *elems = hegel_integers(0, 100);
    assert_non_null(elems);

    hegel_generator *gen = hegel_lists(elems, 1, 10);
    assert_non_null(gen);

    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_non_null(basic);

    char *type = get_map_string(basic->schema, "type");
    assert_non_null(type);
    assert_string_equal(type, "list");
    free(type);

    assert_int_equal(get_map_int(basic->schema, "min_size"), 1);
    assert_int_equal(get_map_int(basic->schema, "max_size"), 10);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: dicts schema (basic keys and values)
 * ------------------------------------------------------------------ */
static void test_dicts_schema(void **state)
{
    (void)state;

    hegel_generator *keys = hegel_text(1, 5);
    hegel_generator *vals = hegel_integers(0, 100);
    assert_non_null(keys);
    assert_non_null(vals);

    hegel_generator *gen = hegel_dicts(keys, vals, 0, 5);
    assert_non_null(gen);

    /* Dicts always use the compositional (collection protocol) path because
     * the server's "dict" schema returns a list of [key, value] pairs, not
     * a CBOR map.  as_basic returns NULL to force this path. */
    hegel_basic_gen *basic = gen->vtable.as_basic(gen);
    assert_null(basic);

    hegel_generator_free(gen);
}

/* ------------------------------------------------------------------
 * Test: cbor_unwrap_tags on untagged items (passthrough)
 * ------------------------------------------------------------------ */
static void test_unwrap_tags_passthrough(void **state)
{
    (void)state;

    /* An untagged integer should pass through unchanged */
    cbor_item_t *num = cbor_build_uint64(42);
    assert_non_null(num);

    const cbor_item_t *inner = cbor_unwrap_tags(num);
    assert_ptr_equal(inner, num);
    assert_true(cbor_isa_uint(inner));
    assert_int_equal(cbor_get_uint64(inner), 42);

    cbor_decref(&num);

    /* NULL should pass through as NULL */
    assert_null(cbor_unwrap_tags(NULL));
}

/* ------------------------------------------------------------------
 * Test: cbor_unwrap_tags on singly-tagged items
 * ------------------------------------------------------------------ */
static void test_unwrap_tags_single(void **state)
{
    (void)state;

    /* Wrap a string in tag 91 (text as bytestring, common from Python cbor2) */
    cbor_item_t *str = cbor_build_string("hello");
    assert_non_null(str);

    cbor_item_t *tagged = cbor_new_tag(91);
    assert_non_null(tagged);
    cbor_tag_set_item(tagged, cbor_move(str));

    /* The tagged item should be a tag */
    assert_true(cbor_isa_tag(tagged));

    /* Unwrapping should reach the inner string */
    const cbor_item_t *inner = cbor_unwrap_tags(tagged);
    assert_non_null(inner);
    assert_true(cbor_isa_string(inner));
    assert_int_equal(cbor_string_length(inner), 5);
    assert_memory_equal(cbor_string_handle(inner), "hello", 5);

    cbor_decref(&tagged);
}

/* ------------------------------------------------------------------
 * Test: cbor_unwrap_tags on nested tags (multiple layers)
 * ------------------------------------------------------------------ */
static void test_unwrap_tags_nested(void **state)
{
    (void)state;

    /* Build: tag(1, tag(2, 99)) -- two tag layers around an integer */
    cbor_item_t *num = cbor_build_uint8(99);
    assert_non_null(num);

    cbor_item_t *inner_tag = cbor_new_tag(2);
    assert_non_null(inner_tag);
    cbor_tag_set_item(inner_tag, cbor_move(num));

    cbor_item_t *outer_tag = cbor_new_tag(1);
    assert_non_null(outer_tag);
    cbor_tag_set_item(outer_tag, cbor_move(inner_tag));

    assert_true(cbor_isa_tag(outer_tag));

    const cbor_item_t *inner = cbor_unwrap_tags(outer_tag);
    assert_non_null(inner);
    assert_true(cbor_isa_uint(inner));
    assert_int_equal(cbor_get_uint8(inner), 99);

    cbor_decref(&outer_tag);
}

/* ------------------------------------------------------------------
 * Test: cbor_get_string handles tagged bytestrings
 *
 * The server (Python cbor2) may wrap text as tag(91, bytestring).
 * cbor_get_string() calls cbor_unwrap_tags() internally.
 * ------------------------------------------------------------------ */
static void test_get_string_tagged_bytestring(void **state)
{
    (void)state;

    /* Build tag(91, b"world") */
    cbor_item_t *bs = cbor_build_bytestring((const cbor_data)"world", 5);
    assert_non_null(bs);

    cbor_item_t *tagged = cbor_new_tag(91);
    assert_non_null(tagged);
    cbor_tag_set_item(tagged, cbor_move(bs));

    char *result = cbor_get_string(tagged);
    assert_non_null(result);
    assert_string_equal(result, "world");
    free(result);

    cbor_decref(&tagged);
}

/* ------------------------------------------------------------------
 * Test: cbor_get_string handles tagged text strings
 * ------------------------------------------------------------------ */
static void test_get_string_tagged_text(void **state)
{
    (void)state;

    /* Build tag(42, "tagged text") */
    cbor_item_t *str = cbor_build_string("tagged text");
    assert_non_null(str);

    cbor_item_t *tagged = cbor_new_tag(42);
    assert_non_null(tagged);
    cbor_tag_set_item(tagged, cbor_move(str));

    char *result = cbor_get_string(tagged);
    assert_non_null(result);
    assert_string_equal(result, "tagged text");
    free(result);

    cbor_decref(&tagged);
}

/* ------------------------------------------------------------------
 * Test: NULL and edge case handling
 * ------------------------------------------------------------------ */
static void test_generator_null_inputs(void **state)
{
    (void)state;

    /* sampled_from with NULL */
    assert_null(hegel_sampled_from_strings(NULL, 0));
    assert_null(hegel_sampled_from_strings(NULL, 3));

    /* sampled_from with count 0 */
    const char *vals[] = {"a"};
    assert_null(hegel_sampled_from_strings(vals, 0));

    /* sampled_from_floats with NULL/0 */
    assert_null(hegel_sampled_from_floats(NULL, 0));
    assert_null(hegel_sampled_from_floats(NULL, 3));
    const double fvals[] = {1.0};
    assert_null(hegel_sampled_from_floats(fvals, 0));

    /* sampled_from_bools with NULL/0 */
    assert_null(hegel_sampled_from_bools(NULL, 0));
    assert_null(hegel_sampled_from_bools(NULL, 2));
    const bool bvals[] = {true};
    assert_null(hegel_sampled_from_bools(bvals, 0));

    /* map with NULL source */
    assert_null(hegel_map(NULL, double_int, NULL, NULL));

    /* filter with NULL source */
    assert_null(hegel_filter(NULL, always_true, NULL));

    /* flat_map with NULL source */
    assert_null(hegel_flat_map(NULL, flatmap_fn, NULL));

    /* one_of with NULL */
    assert_null(hegel_one_of(NULL, 0));
    assert_null(hegel_one_of(NULL, 2));

    /* lists with NULL elements */
    assert_null(hegel_lists(NULL, 0, 10));

    /* dicts with NULL keys/values */
    assert_null(hegel_dicts(NULL, NULL, 0, 5));

    /* just_string with NULL */
    assert_null(hegel_just_string(NULL));

    /* from_regex with NULL */
    assert_null(hegel_from_regex(NULL, false));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_integers_schema),
        cmocka_unit_test(test_integers_negative),
        cmocka_unit_test(test_booleans_schema),
        cmocka_unit_test(test_text_schema),
        cmocka_unit_test(test_binary_schema),
        cmocka_unit_test(test_nulls_schema),
        cmocka_unit_test(test_arrays_alias),
        cmocka_unit_test(test_just_null_schema),
        cmocka_unit_test(test_sampled_from_strings_schema),
        cmocka_unit_test(test_sampled_from_floats_schema),
        cmocka_unit_test(test_sampled_from_bools_schema),
        cmocka_unit_test(test_map_preserves_basic),
        cmocka_unit_test(test_filter_becomes_non_basic),
        cmocka_unit_test(test_flat_map_non_basic),
        cmocka_unit_test(test_one_of_all_basic),
        cmocka_unit_test(test_floats_schema),
        cmocka_unit_test(test_lists_schema),
        cmocka_unit_test(test_dicts_schema),
        cmocka_unit_test(test_generator_null_inputs),
        cmocka_unit_test(test_unwrap_tags_passthrough),
        cmocka_unit_test(test_unwrap_tags_single),
        cmocka_unit_test(test_unwrap_tags_nested),
        cmocka_unit_test(test_get_string_tagged_bytestring),
        cmocka_unit_test(test_get_string_tagged_text),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
