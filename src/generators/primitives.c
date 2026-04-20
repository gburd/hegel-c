#include "generator.h"
#include "../protocol/cbor_helpers.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ================================================================
 * Helper: create a basic-only generator from a schema + optional transform
 * ================================================================ */

typedef struct {
    hegel_basic_gen basic;
} basic_gen_data;

static hegel_basic_gen *basic_as_basic(hegel_generator *self)
{
    basic_gen_data *d = (basic_gen_data *)self->data;
    return &d->basic;
}

static void basic_free(hegel_generator *self)
{
    basic_gen_data *d = (basic_gen_data *)self->data;
    if (d) {
        if (d->basic.schema)
            cbor_decref(&d->basic.schema);
        if (d->basic.free_ctx && d->basic.transform_ctx)
            d->basic.free_ctx(d->basic.transform_ctx);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable basic_vtable = {
    .draw = NULL,  /* basic generators always use schema path */
    .as_basic = basic_as_basic,
    .free = basic_free
};

static hegel_generator *make_basic_gen(cbor_item_t *schema,
                                        void *(*transform)(cbor_item_t *, void *),
                                        void *transform_ctx,
                                        void (*free_ctx)(void *))
{
    if (!schema)
        return NULL;

    basic_gen_data *d = calloc(1, sizeof(basic_gen_data));
    if (!d) {
        cbor_decref(&schema);
        return NULL;
    }
    d->basic.schema = schema;
    d->basic.transform = transform;
    d->basic.transform_ctx = transform_ctx;
    d->basic.free_ctx = free_ctx;

    hegel_generator *gen = hegel_generator_alloc(basic_vtable, d);
    if (!gen) {
        cbor_decref(&schema);
        free(d);
        return NULL;
    }
    return gen;
}

/* ================================================================
 * Integers
 * ================================================================ */

hegel_generator *hegel_integers(int64_t min_value, int64_t max_value)
{
    /* Schema: {"type": "integer", "min_value": min, "max_value": max} */
    cbor_item_t *schema = cbor_new_definite_map(3);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "integer");
    cbor_map_add_int(schema, "min_value", min_value);
    cbor_map_add_int(schema, "max_value", max_value);

    return make_basic_gen(schema, NULL, NULL, NULL);
}

/* ================================================================
 * Floats
 * ================================================================ */

hegel_generator *hegel_floats(double min_value, double max_value)
{
    return hegel_floats_ex(min_value, max_value, false, false, false, false, 64);
}

hegel_generator *hegel_floats_ex(double min_value, double max_value,
                                  bool exclude_min, bool exclude_max,
                                  bool allow_nan, bool allow_infinity, int width)
{
    /* Schema: {"type": "float", "min_value": min, "max_value": max, ...} */
    cbor_item_t *schema = cbor_new_definite_map(7);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "float");

    /* Add min_value as a CBOR float */
    cbor_item_t *k_min = cbor_build_string("min_value");
    cbor_item_t *v_min = cbor_build_float8(min_value);
    if (k_min && v_min) {
        struct cbor_pair pair = {.key = cbor_move(k_min), .value = cbor_move(v_min)};
        cbor_map_add(schema, pair);
    } else {
        if (k_min) cbor_decref(&k_min);
        if (v_min) cbor_decref(&v_min);
    }

    cbor_item_t *k_max = cbor_build_string("max_value");
    cbor_item_t *v_max = cbor_build_float8(max_value);
    if (k_max && v_max) {
        struct cbor_pair pair = {.key = cbor_move(k_max), .value = cbor_move(v_max)};
        cbor_map_add(schema, pair);
    } else {
        if (k_max) cbor_decref(&k_max);
        if (v_max) cbor_decref(&v_max);
    }

    cbor_map_add_bool(schema, "exclude_min", exclude_min);
    cbor_map_add_bool(schema, "exclude_max", exclude_max);
    cbor_map_add_bool(schema, "allow_nan", allow_nan);
    cbor_map_add_bool(schema, "allow_infinity", allow_infinity);
    cbor_map_add_int(schema, "width", (int64_t)width);

    return make_basic_gen(schema, NULL, NULL, NULL);
}

/* ================================================================
 * Booleans
 * ================================================================ */

hegel_generator *hegel_booleans(void)
{
    /* Schema: {"type": "boolean"} */
    cbor_item_t *schema = cbor_new_definite_map(1);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "boolean");

    return make_basic_gen(schema, NULL, NULL, NULL);
}

hegel_generator *hegel_booleans_p(double p)
{
    /* Schema: {"type": "boolean", "p": p} */
    cbor_item_t *schema = cbor_new_definite_map(2);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "boolean");

    cbor_item_t *k = cbor_build_string("p");
    cbor_item_t *v = cbor_build_float8(p);
    if (k && v) {
        struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
        cbor_map_add(schema, pair);
    } else {
        if (k) cbor_decref(&k);
        if (v) cbor_decref(&v);
    }

    return make_basic_gen(schema, NULL, NULL, NULL);
}

/* ================================================================
 * Text
 * ================================================================ */

hegel_generator *hegel_text(size_t min_size, size_t max_size)
{
    /* Schema: {"type": "string", "min_size": min, "max_size": max} */
    cbor_item_t *schema = cbor_new_definite_map(3);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "string");
    cbor_map_add_int(schema, "min_size", (int64_t)min_size);
    cbor_map_add_int(schema, "max_size", (int64_t)max_size);

    return make_basic_gen(schema, NULL, NULL, NULL);
}

/* ================================================================
 * Binary
 * ================================================================ */

hegel_generator *hegel_binary(size_t min_size, size_t max_size)
{
    /* Schema: {"type": "binary", "min_size": min, "max_size": max} */
    cbor_item_t *schema = cbor_new_definite_map(3);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "binary");
    cbor_map_add_int(schema, "min_size", (int64_t)min_size);
    cbor_map_add_int(schema, "max_size", (int64_t)max_size);

    return make_basic_gen(schema, NULL, NULL, NULL);
}

/* ================================================================
 * Just (constant) generators
 *
 * Schema is always {"constant": null}.
 * The transform ignores the raw value and returns the stored constant.
 * ================================================================ */

/* -- just_null -- */
static void *just_null_transform(cbor_item_t *raw, void *ctx)
{
    (void)raw;
    (void)ctx;
    return NULL;
}

hegel_generator *hegel_just_null(void)
{
    cbor_item_t *schema = cbor_new_definite_map(1);
    if (!schema)
        return NULL;

    /* Add "constant": null */
    cbor_item_t *k = cbor_build_string("constant");
    cbor_item_t *v = cbor_new_null();
    if (k && v) {
        struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
        cbor_map_add(schema, pair);
    } else {
        if (k) cbor_decref(&k);
        if (v) cbor_decref(&v);
        cbor_decref(&schema);
        return NULL;
    }

    return make_basic_gen(schema, just_null_transform, NULL, NULL);
}

/* -- just_int -- */
typedef struct {
    int64_t value;
} just_int_ctx;

static void *just_int_transform(cbor_item_t *raw, void *ctx)
{
    (void)raw;
    just_int_ctx *c = (just_int_ctx *)ctx;
    int64_t *result = malloc(sizeof(int64_t));
    if (!result)
        return NULL;
    *result = c->value;
    return result;
}

static void just_int_free_ctx(void *ctx)
{
    free(ctx);
}

hegel_generator *hegel_just_int(int64_t value)
{
    cbor_item_t *schema = cbor_new_definite_map(1);
    if (!schema)
        return NULL;

    cbor_item_t *k = cbor_build_string("constant");
    cbor_item_t *v = cbor_new_null();
    if (k && v) {
        struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
        cbor_map_add(schema, pair);
    } else {
        if (k) cbor_decref(&k);
        if (v) cbor_decref(&v);
        cbor_decref(&schema);
        return NULL;
    }

    just_int_ctx *ctx = malloc(sizeof(just_int_ctx));
    if (!ctx) {
        cbor_decref(&schema);
        return NULL;
    }
    ctx->value = value;

    return make_basic_gen(schema, just_int_transform, ctx, just_int_free_ctx);
}

/* -- just_float -- */
typedef struct {
    double value;
} just_float_ctx;

static void *just_float_transform(cbor_item_t *raw, void *ctx)
{
    (void)raw;
    just_float_ctx *c = (just_float_ctx *)ctx;
    double *result = malloc(sizeof(double));
    if (!result)
        return NULL;
    *result = c->value;
    return result;
}

static void just_float_free_ctx(void *ctx)
{
    free(ctx);
}

hegel_generator *hegel_just_float(double value)
{
    cbor_item_t *schema = cbor_new_definite_map(1);
    if (!schema)
        return NULL;

    cbor_item_t *k = cbor_build_string("constant");
    cbor_item_t *v = cbor_new_null();
    if (k && v) {
        struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
        cbor_map_add(schema, pair);
    } else {
        if (k) cbor_decref(&k);
        if (v) cbor_decref(&v);
        cbor_decref(&schema);
        return NULL;
    }

    just_float_ctx *ctx = malloc(sizeof(just_float_ctx));
    if (!ctx) {
        cbor_decref(&schema);
        return NULL;
    }
    ctx->value = value;

    return make_basic_gen(schema, just_float_transform, ctx, just_float_free_ctx);
}

/* -- just_string -- */
typedef struct {
    char *value;
} just_string_ctx;

static void *just_string_transform(cbor_item_t *raw, void *ctx)
{
    (void)raw;
    just_string_ctx *c = (just_string_ctx *)ctx;
    return strdup(c->value);
}

static void just_string_free_ctx(void *ctx)
{
    just_string_ctx *c = (just_string_ctx *)ctx;
    free(c->value);
    free(c);
}

hegel_generator *hegel_just_string(const char *value)
{
    if (!value)
        return NULL;

    cbor_item_t *schema = cbor_new_definite_map(1);
    if (!schema)
        return NULL;

    cbor_item_t *k = cbor_build_string("constant");
    cbor_item_t *v = cbor_new_null();
    if (k && v) {
        struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
        cbor_map_add(schema, pair);
    } else {
        if (k) cbor_decref(&k);
        if (v) cbor_decref(&v);
        cbor_decref(&schema);
        return NULL;
    }

    just_string_ctx *ctx = malloc(sizeof(just_string_ctx));
    if (!ctx) {
        cbor_decref(&schema);
        return NULL;
    }
    ctx->value = strdup(value);
    if (!ctx->value) {
        cbor_decref(&schema);
        free(ctx);
        return NULL;
    }

    return make_basic_gen(schema, just_string_transform, ctx, just_string_free_ctx);
}

/* -- just_bool -- */
typedef struct {
    bool value;
} just_bool_ctx;

static void *just_bool_transform(cbor_item_t *raw, void *ctx)
{
    (void)raw;
    just_bool_ctx *c = (just_bool_ctx *)ctx;
    bool *result = malloc(sizeof(bool));
    if (!result)
        return NULL;
    *result = c->value;
    return result;
}

static void just_bool_free_ctx(void *ctx)
{
    free(ctx);
}

hegel_generator *hegel_just_bool(bool value)
{
    cbor_item_t *schema = cbor_new_definite_map(1);
    if (!schema)
        return NULL;

    cbor_item_t *k = cbor_build_string("constant");
    cbor_item_t *v = cbor_new_null();
    if (k && v) {
        struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
        cbor_map_add(schema, pair);
    } else {
        if (k) cbor_decref(&k);
        if (v) cbor_decref(&v);
        cbor_decref(&schema);
        return NULL;
    }

    just_bool_ctx *ctx = malloc(sizeof(just_bool_ctx));
    if (!ctx) {
        cbor_decref(&schema);
        return NULL;
    }
    ctx->value = value;

    return make_basic_gen(schema, just_bool_transform, ctx, just_bool_free_ctx);
}

/* ================================================================
 * Sampled-from generators
 *
 * Schema: {"type":"integer","min_value":0,"max_value":n-1}
 * Transform: index -> values[index]
 * ================================================================ */

/* -- sampled_from_strings -- */
typedef struct {
    char **values;
    size_t count;
} sampled_strings_ctx;

static void *sampled_strings_transform(cbor_item_t *raw, void *ctx)
{
    sampled_strings_ctx *c = (sampled_strings_ctx *)ctx;
    uint64_t idx = 0;
    if (cbor_isa_uint(raw))
        idx = cbor_get_uint_value(raw);
    if (idx >= c->count)
        idx = 0;
    return strdup(c->values[idx]);
}

static void sampled_strings_free_ctx(void *ctx)
{
    sampled_strings_ctx *c = (sampled_strings_ctx *)ctx;
    for (size_t i = 0; i < c->count; i++)
        free(c->values[i]);
    free(c->values);
    free(c);
}

hegel_generator *hegel_sampled_from_strings(const char **values, size_t count)
{
    if (!values || count == 0)
        return NULL;

    cbor_item_t *schema = cbor_new_definite_map(3);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "integer");
    cbor_map_add_int(schema, "min_value", 0);
    cbor_map_add_int(schema, "max_value", (int64_t)(count - 1));

    sampled_strings_ctx *ctx = malloc(sizeof(sampled_strings_ctx));
    if (!ctx) {
        cbor_decref(&schema);
        return NULL;
    }
    ctx->count = count;
    ctx->values = malloc(count * sizeof(char *));
    if (!ctx->values) {
        cbor_decref(&schema);
        free(ctx);
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        ctx->values[i] = strdup(values[i]);
        if (!ctx->values[i]) {
            for (size_t j = 0; j < i; j++)
                free(ctx->values[j]);
            free(ctx->values);
            free(ctx);
            cbor_decref(&schema);
            return NULL;
        }
    }

    return make_basic_gen(schema, sampled_strings_transform, ctx, sampled_strings_free_ctx);
}

/* -- sampled_from_ints -- */
typedef struct {
    int64_t *values;
    size_t count;
} sampled_ints_ctx;

static void *sampled_ints_transform(cbor_item_t *raw, void *ctx)
{
    sampled_ints_ctx *c = (sampled_ints_ctx *)ctx;
    uint64_t idx = 0;
    if (cbor_isa_uint(raw))
        idx = cbor_get_uint_value(raw);
    if (idx >= c->count)
        idx = 0;
    int64_t *result = malloc(sizeof(int64_t));
    if (!result)
        return NULL;
    *result = c->values[idx];
    return result;
}

static void sampled_ints_free_ctx(void *ctx)
{
    sampled_ints_ctx *c = (sampled_ints_ctx *)ctx;
    free(c->values);
    free(c);
}

hegel_generator *hegel_sampled_from_ints(const int64_t *values, size_t count)
{
    if (!values || count == 0)
        return NULL;

    cbor_item_t *schema = cbor_new_definite_map(3);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "integer");
    cbor_map_add_int(schema, "min_value", 0);
    cbor_map_add_int(schema, "max_value", (int64_t)(count - 1));

    sampled_ints_ctx *ctx = malloc(sizeof(sampled_ints_ctx));
    if (!ctx) {
        cbor_decref(&schema);
        return NULL;
    }
    ctx->count = count;
    ctx->values = malloc(count * sizeof(int64_t));
    if (!ctx->values) {
        cbor_decref(&schema);
        free(ctx);
        return NULL;
    }
    memcpy(ctx->values, values, count * sizeof(int64_t));

    return make_basic_gen(schema, sampled_ints_transform, ctx, sampled_ints_free_ctx);
}

/* Regex and format generators are in formats.c */
