#include "generator.h"
#include "../protocol/cbor_helpers.h"
#include "hegel/hegel.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Map combinator
 *
 * CENTRAL OPTIMIZATION: If the source generator is basic, the mapped
 * generator is ALSO basic with the same schema.  The transform is
 * composed: new_transform = user_fn(original_transform(raw)).
 *
 * If the source is non-basic, the map wraps it compositionally with
 * a MAPPED span.
 * ================================================================ */

/* Composed transform context: chains source_transform -> user_fn */
typedef struct {
    /* The original basic generator's transform (may be NULL = identity) */
    void *(*source_transform)(cbor_item_t *raw, void *ctx);
    void *source_ctx;
    /* The user's map function */
    hegel_map_fn user_fn;
    void *user_ctx;
    /* Freeing */
    hegel_free_fn user_free_fn;
} map_compose_ctx;

static void *map_composed_transform(cbor_item_t *raw, void *ctx)
{
    map_compose_ctx *mc = (map_compose_ctx *)ctx;

    /* Step 1: apply source transform (or pass raw through) */
    void *intermediate;
    if (mc->source_transform) {
        intermediate = mc->source_transform(raw, mc->source_ctx);
    } else {
        /* Identity: pass the CBOR item.  We incref so the caller's
         * decref of raw doesn't invalidate it. */
        cbor_incref(raw);
        intermediate = raw;
    }

    if (!intermediate)
        return NULL;

    /* Step 2: apply user map function */
    void *result = mc->user_fn(intermediate, mc->user_ctx);

    /* Free intermediate if it differs from result and was allocated.
     * For basic generators with identity source_transform, intermediate
     * is a cbor_item_t* that we incref'd.  The user_fn is responsible
     * for the semantics here -- if they consumed intermediate, fine.
     * We free intermediate using the user's free function if provided
     * and it differs from result. */
    if (intermediate != result) {
        if (mc->user_free_fn) {
            mc->user_free_fn(intermediate);
        } else if (!mc->source_transform) {
            /* intermediate was an incref'd cbor_item_t */
            cbor_item_t *tmp = (cbor_item_t *)intermediate;
            cbor_decref(&tmp);
        }
    }

    return result;
}

static void map_compose_ctx_free(void *ctx)
{
    map_compose_ctx *mc = (map_compose_ctx *)ctx;
    /* We don't own source_ctx -- it belongs to the source generator.
     * We also don't own user_ctx -- the caller manages it.
     * We just free the compose struct itself. */
    free(mc);
}

/* Data for map generator (both basic-preserving and non-basic cases) */
typedef struct {
    hegel_generator *source;
    hegel_map_fn fn;
    void *ctx;
    hegel_free_fn free_fn;

    /* For basic-preserving map: composed basic gen info */
    bool is_basic;
    hegel_basic_gen basic;
    map_compose_ctx *compose;
} map_gen_data;

static hegel_basic_gen *map_as_basic(hegel_generator *self)
{
    map_gen_data *d = (map_gen_data *)self->data;
    if (d->is_basic)
        return &d->basic;
    return NULL;
}

static void *map_draw(hegel_generator *self, hegel_test_case *tc)
{
    map_gen_data *d = (map_gen_data *)self->data;

    /* Non-basic map: compositional with MAPPED span */
    hegel_send_start_span(tc, HEGEL_SPAN_MAPPED);

    void *raw_value = hegel_draw_internal(tc, d->source);

    hegel_send_stop_span(tc, false);

    if (!raw_value)
        return NULL;

    void *result = d->fn(raw_value, d->ctx);

    /* Free the intermediate value if it differs from result */
    if (raw_value != result && d->free_fn) {
        d->free_fn(raw_value);
    }

    return result;
}

static void map_free(hegel_generator *self)
{
    map_gen_data *d = (map_gen_data *)self->data;
    if (d) {
        if (d->is_basic) {
            /* Schema is borrowed from source via compose; compose_ctx frees itself */
            if (d->compose)
                map_compose_ctx_free(d->compose);
        }
        hegel_generator_free(d->source);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable map_vtable = {
    .draw = map_draw,
    .as_basic = map_as_basic,
    .free = map_free
};

hegel_generator *hegel_map(hegel_generator *source, hegel_map_fn fn, void *ctx,
                            hegel_free_fn free_fn)
{
    if (!source || !fn)
        return NULL;

    map_gen_data *d = calloc(1, sizeof(map_gen_data));
    if (!d)
        return NULL;
    d->source = source;
    d->fn = fn;
    d->ctx = ctx;
    d->free_fn = free_fn;
    d->is_basic = false;

    /* Check if source is basic -- if so, PRESERVE basic-ness */
    hegel_basic_gen *source_basic = NULL;
    if (source->vtable.as_basic)
        source_basic = source->vtable.as_basic(source);

    if (source_basic) {
        /* Central optimization: compose transforms, keep schema */
        map_compose_ctx *compose = calloc(1, sizeof(map_compose_ctx));
        if (!compose) {
            free(d);
            return NULL;
        }
        compose->source_transform = source_basic->transform;
        compose->source_ctx = source_basic->transform_ctx;
        compose->user_fn = fn;
        compose->user_ctx = ctx;
        compose->user_free_fn = free_fn;

        d->is_basic = true;
        d->compose = compose;

        /* The basic gen uses the source's schema with composed transform */
        d->basic.schema = source_basic->schema;  /* borrowed, owned by source */
        d->basic.transform = map_composed_transform;
        d->basic.transform_ctx = compose;
        d->basic.free_ctx = NULL; /* compose is freed in map_free */
    }

    return hegel_generator_alloc(map_vtable, d);
}

/* ================================================================
 * FlatMap combinator
 *
 * Always non-basic.
 * Draw: start_span(FLAT_MAP), draw from source, call fn to get inner gen,
 *       draw from inner, stop_span(false).
 * ================================================================ */

typedef struct {
    hegel_generator *source;
    hegel_flatmap_fn fn;
    void *ctx;
} flatmap_gen_data;

static void *flatmap_draw(hegel_generator *self, hegel_test_case *tc)
{
    flatmap_gen_data *d = (flatmap_gen_data *)self->data;

    hegel_send_start_span(tc, HEGEL_SPAN_FLAT_MAP);

    /* Draw from source */
    void *source_value = hegel_draw_internal(tc, d->source);
    if (!source_value) {
        hegel_send_stop_span(tc, false);
        return NULL;
    }

    /* Call fn to get inner generator */
    hegel_generator *inner = d->fn(source_value, d->ctx);
    if (!inner) {
        hegel_send_stop_span(tc, false);
        return NULL;
    }

    /* Draw from inner generator */
    void *result = hegel_draw_internal(tc, inner);

    hegel_send_stop_span(tc, false);

    /* Free the inner generator (it was freshly created by fn) */
    hegel_generator_free(inner);

    return result;
}

static hegel_basic_gen *flatmap_as_basic(hegel_generator *self)
{
    (void)self;
    return NULL; /* always non-basic */
}

static void flatmap_free(hegel_generator *self)
{
    flatmap_gen_data *d = (flatmap_gen_data *)self->data;
    if (d) {
        hegel_generator_free(d->source);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable flatmap_vtable = {
    .draw = flatmap_draw,
    .as_basic = flatmap_as_basic,
    .free = flatmap_free
};

hegel_generator *hegel_flat_map(hegel_generator *source, hegel_flatmap_fn fn, void *ctx)
{
    if (!source || !fn)
        return NULL;

    flatmap_gen_data *d = calloc(1, sizeof(flatmap_gen_data));
    if (!d)
        return NULL;
    d->source = source;
    d->fn = fn;
    d->ctx = ctx;

    return hegel_generator_alloc(flatmap_vtable, d);
}

/* ================================================================
 * Filter combinator
 *
 * Always non-basic.
 * Draw: try 3 times:
 *   1. start_span(FILTER)
 *   2. Draw from source
 *   3. If predicate passes: stop_span(false), return
 *   4. If predicate fails: stop_span(true) (discard)
 *   5. After 3 failures: hegel_assume(false)
 * ================================================================ */

#define FILTER_MAX_RETRIES 3

typedef struct {
    hegel_generator *source;
    hegel_predicate_fn pred;
    void *ctx;
} filter_gen_data;

static void *filter_draw(hegel_generator *self, hegel_test_case *tc)
{
    filter_gen_data *d = (filter_gen_data *)self->data;

    for (int attempt = 0; attempt < FILTER_MAX_RETRIES; attempt++) {
        hegel_send_start_span(tc, HEGEL_SPAN_FILTER);

        void *value = hegel_draw_internal(tc, d->source);
        if (!value) {
            hegel_send_stop_span(tc, true);
            continue;
        }

        if (d->pred(value, d->ctx)) {
            /* Predicate passed */
            hegel_send_stop_span(tc, false);
            return value;
        }

        /* Predicate failed -- discard this span */
        hegel_send_stop_span(tc, true);
        /* We need to free the rejected value.  Since it's a cbor_item_t*
         * in the basic case or a user-allocated value in the non-basic case,
         * we treat it as a cbor_item_t* if the source is basic. */
        cbor_item_t *item = (cbor_item_t *)value;
        cbor_decref(&item);
    }

    /* All retries exhausted */
    hegel_assume(false);
    return NULL; /* unreachable if hegel_assume aborts */
}

static hegel_basic_gen *filter_as_basic(hegel_generator *self)
{
    (void)self;
    return NULL; /* always non-basic */
}

static void filter_free(hegel_generator *self)
{
    filter_gen_data *d = (filter_gen_data *)self->data;
    if (d) {
        hegel_generator_free(d->source);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable filter_vtable = {
    .draw = filter_draw,
    .as_basic = filter_as_basic,
    .free = filter_free
};

hegel_generator *hegel_filter(hegel_generator *source, hegel_predicate_fn pred, void *ctx)
{
    if (!source || !pred)
        return NULL;

    filter_gen_data *d = calloc(1, sizeof(filter_gen_data));
    if (!d)
        return NULL;
    d->source = source;
    d->pred = pred;
    d->ctx = ctx;

    return hegel_generator_alloc(filter_vtable, d);
}

/* ================================================================
 * One-of combinator
 *
 * Case 1: ALL basic, ALL identity transforms
 *   Schema: {"one_of": [<schema0>, <schema1>, ...]}
 *
 * Case 2: ALL basic, SOME have transforms
 *   Schema: {"one_of": [{"type":"tuple","elements":[{"constant":0},<schema0>]}, ...]}
 *   Transform: read tag from tuple[0], apply branch's transform to tuple[1]
 *
 * Case 3: ANY non-basic
 *   Compositional: generate index via integers(0, count-1),
 *   then start_span(ONE_OF), draw from generators[index], stop_span(false)
 * ================================================================ */

typedef struct {
    hegel_generator **generators;
    size_t count;
    /* Cached basic gen info */
    hegel_basic_gen **elem_basics;
    bool all_basic;
    bool all_identity; /* all basic and no transforms */
} oneof_gen_data;

/* Transform for Case 2: tagged tuple approach */
typedef struct {
    void *(**transforms)(cbor_item_t *, void *);
    void **transform_ctxs;
    size_t count;
} oneof_tagged_ctx;

static void *oneof_tagged_transform(cbor_item_t *raw, void *ctx)
{
    oneof_tagged_ctx *oc = (oneof_tagged_ctx *)ctx;

    if (!cbor_isa_array(raw) || cbor_array_size(raw) != 2)
        return NULL;

    /* raw is [tag, value] */
    cbor_item_t *tag_item = cbor_array_get(raw, 0);
    cbor_item_t *value_item = cbor_array_get(raw, 1);

    uint64_t tag = 0;
    if (cbor_isa_uint(tag_item))
        tag = cbor_get_int_value(tag_item);
    if (tag >= oc->count)
        return NULL;

    if (oc->transforms[tag]) {
        return oc->transforms[tag](value_item, oc->transform_ctxs[tag]);
    }

    /* Identity transform: return the value as-is (incref) */
    cbor_incref(value_item);
    return value_item;
}

static hegel_basic_gen *oneof_as_basic(hegel_generator *self)
{
    oneof_gen_data *d = (oneof_gen_data *)self->data;
    if (!d->all_basic)
        return NULL;

    static __thread hegel_basic_gen cached;
    static __thread cbor_item_t *cached_schema = NULL;
    static __thread oneof_tagged_ctx *cached_tagged = NULL;

    if (cached_schema) {
        cbor_decref(&cached_schema);
        cached_schema = NULL;
    }
    if (cached_tagged) {
        free(cached_tagged->transforms);
        free(cached_tagged->transform_ctxs);
        free(cached_tagged);
        cached_tagged = NULL;
    }

    /* Re-check elem_basics (they're cached from construction) */
    if (!d->elem_basics)
        return NULL;

    if (d->all_identity) {
        /* Case 1: simple one_of with raw schemas */
        cbor_item_t *schemas_arr = cbor_new_definite_array(d->count);
        if (!schemas_arr)
            return NULL;

        for (size_t i = 0; i < d->count; i++) {
            cbor_incref(d->elem_basics[i]->schema);
            cbor_array_push(schemas_arr, cbor_move(d->elem_basics[i]->schema));
        }

        cached_schema = cbor_new_definite_map(1);
        if (!cached_schema) {
            cbor_decref(&schemas_arr);
            return NULL;
        }
        cbor_map_add_item(cached_schema, "one_of", schemas_arr);
        cbor_decref(&schemas_arr);

        cached.schema = cached_schema;
        cached.transform = NULL;
        cached.transform_ctx = NULL;
        cached.free_ctx = NULL;
    } else {
        /* Case 2: tagged tuples */
        cbor_item_t *schemas_arr = cbor_new_definite_array(d->count);
        if (!schemas_arr)
            return NULL;

        for (size_t i = 0; i < d->count; i++) {
            /* Build: {"type":"tuple","elements":[{"constant":<i>},<schema_i>]} */
            cbor_item_t *const_schema = cbor_new_definite_map(1);
            if (!const_schema) {
                cbor_decref(&schemas_arr);
                return NULL;
            }
            cbor_map_add_int(const_schema, "constant", (int64_t)i);

            cbor_item_t *tuple_elements = cbor_new_definite_array(2);
            if (!tuple_elements) {
                cbor_decref(&const_schema);
                cbor_decref(&schemas_arr);
                return NULL;
            }
            cbor_array_push(tuple_elements, cbor_move(const_schema));
            cbor_incref(d->elem_basics[i]->schema);
            cbor_array_push(tuple_elements, cbor_move(d->elem_basics[i]->schema));

            cbor_item_t *tuple_schema = cbor_new_definite_map(2);
            if (!tuple_schema) {
                cbor_decref(&tuple_elements);
                cbor_decref(&schemas_arr);
                return NULL;
            }
            cbor_map_add_string(tuple_schema, "type", "tuple");
            cbor_map_add_item(tuple_schema, "elements", tuple_elements);
            cbor_decref(&tuple_elements);

            cbor_array_push(schemas_arr, cbor_move(tuple_schema));
        }

        cached_schema = cbor_new_definite_map(1);
        if (!cached_schema) {
            cbor_decref(&schemas_arr);
            return NULL;
        }
        cbor_map_add_item(cached_schema, "one_of", schemas_arr);
        cbor_decref(&schemas_arr);

        /* Build tagged transform context */
        cached_tagged = calloc(1, sizeof(oneof_tagged_ctx));
        if (!cached_tagged)
            return NULL;
        cached_tagged->count = d->count;
        cached_tagged->transforms = calloc(d->count, sizeof(void *));
        cached_tagged->transform_ctxs = calloc(d->count, sizeof(void *));
        if (!cached_tagged->transforms || !cached_tagged->transform_ctxs) {
            free(cached_tagged->transforms);
            free(cached_tagged->transform_ctxs);
            free(cached_tagged);
            cached_tagged = NULL;
            return NULL;
        }
        for (size_t i = 0; i < d->count; i++) {
            cached_tagged->transforms[i] = d->elem_basics[i]->transform;
            cached_tagged->transform_ctxs[i] = d->elem_basics[i]->transform_ctx;
        }

        cached.schema = cached_schema;
        cached.transform = oneof_tagged_transform;
        cached.transform_ctx = cached_tagged;
        cached.free_ctx = NULL;
    }

    return &cached;
}

/* Compositional draw for non-basic one_of (Case 3) */
static void *oneof_draw(hegel_generator *self, hegel_test_case *tc)
{
    oneof_gen_data *d = (oneof_gen_data *)self->data;

    /* Generate index in [0, count-1] */
    hegel_generator *idx_gen = hegel_integers(0, (int64_t)(d->count - 1));
    if (!idx_gen)
        return NULL;

    void *idx_raw = hegel_draw_internal(tc, idx_gen);
    hegel_generator_free(idx_gen);
    if (!idx_raw)
        return NULL;

    /* Extract integer index from CBOR */
    cbor_item_t *idx_item = (cbor_item_t *)idx_raw;
    uint64_t index = 0;
    if (cbor_isa_uint(idx_item))
        index = cbor_get_int_value(idx_item);
    cbor_decref(&idx_item);

    if (index >= d->count)
        index = 0;

    /* Draw from the selected generator */
    hegel_send_start_span(tc, HEGEL_SPAN_ONE_OF);
    void *result = hegel_draw_internal(tc, d->generators[index]);
    hegel_send_stop_span(tc, false);

    return result;
}

static void oneof_free(hegel_generator *self)
{
    oneof_gen_data *d = (oneof_gen_data *)self->data;
    if (d) {
        for (size_t i = 0; i < d->count; i++)
            hegel_generator_free(d->generators[i]);
        free(d->generators);
        free(d->elem_basics);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable oneof_vtable = {
    .draw = oneof_draw,
    .as_basic = oneof_as_basic,
    .free = oneof_free
};

hegel_generator *hegel_one_of(hegel_generator **generators, size_t count)
{
    if (!generators || count == 0)
        return NULL;

    oneof_gen_data *d = calloc(1, sizeof(oneof_gen_data));
    if (!d)
        return NULL;
    d->count = count;
    d->generators = malloc(count * sizeof(hegel_generator *));
    if (!d->generators) {
        free(d);
        return NULL;
    }
    memcpy(d->generators, generators, count * sizeof(hegel_generator *));

    /* Check if all generators are basic */
    d->elem_basics = calloc(count, sizeof(hegel_basic_gen *));
    d->all_basic = true;
    d->all_identity = true;

    if (d->elem_basics) {
        for (size_t i = 0; i < count; i++) {
            if (generators[i]->vtable.as_basic)
                d->elem_basics[i] = generators[i]->vtable.as_basic(generators[i]);

            if (!d->elem_basics[i]) {
                d->all_basic = false;
                d->all_identity = false;
                break;
            }
            if (d->elem_basics[i]->transform) {
                d->all_identity = false;
            }
        }
    } else {
        d->all_basic = false;
        d->all_identity = false;
    }

    return hegel_generator_alloc(oneof_vtable, d);
}

/* ================================================================
 * Optional combinator
 *
 * Implemented as one_of(just_null, element).
 * ================================================================ */

hegel_generator *hegel_optional(hegel_generator *element)
{
    if (!element)
        return NULL;

    hegel_generator *null_gen = hegel_just_null();
    if (!null_gen)
        return NULL;

    hegel_generator *gens[2] = { null_gen, element };
    hegel_generator *result = hegel_one_of(gens, 2);
    if (!result) {
        hegel_generator_free(null_gen);
        /* Don't free element -- caller still owns it on failure */
        return NULL;
    }
    return result;
}
