#include "generator.h"
#include "../protocol/cbor_helpers.h"
#include "hegel/hegel.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Lists
 * ================================================================ */

typedef struct {
    hegel_generator *elements;
    size_t min_size;
    size_t max_size;
    bool unique;
} list_gen_data;

/* Transform for basic list: apply element transform to each item in CBOR array */
typedef struct {
    void *(*elem_transform)(cbor_item_t *raw, void *ctx);
    void *elem_ctx;
} list_basic_ctx;

static void *list_basic_transform(cbor_item_t *raw, void *ctx)
{
    list_basic_ctx *lc = (list_basic_ctx *)ctx;

    if (!cbor_isa_array(raw))
        return NULL;

    size_t n = cbor_array_size(raw);
    /* Build a result: array of void* pointers, terminated by count stored in
     * a hegel_list result structure.  For C, we return a cbor array with
     * transformed items, or if there's no transform, just incref the array. */

    if (!lc || !lc->elem_transform) {
        /* No element transform -- return the raw CBOR array as-is */
        cbor_incref(raw);
        return raw;
    }

    /* Create new CBOR array with transformed elements */
    cbor_item_t *result = cbor_new_definite_array(n);
    if (!result)
        return NULL;

    for (size_t i = 0; i < n; i++) {
        cbor_item_t *elem = cbor_array_get(raw, i);
        void *transformed = lc->elem_transform(elem, lc->elem_ctx);
        if (transformed) {
            /* The transform returns a non-CBOR pointer; for list schema mode
             * we need to keep things as CBOR.  Since this is schema-based,
             * the element transform produces a CBOR item or a C value.
             * For consistency, we'll store transformed results in a flat array.
             * But since the caller (hegel_draw_raw) expects a cbor_item_t*,
             * and basic list schemas with transforms are uncommon (typically
             * the elements are identity-transform), we treat the transformed
             * result as a cbor_item_t*. */
            cbor_item_t *t_item = (cbor_item_t *)transformed;
            (void)cbor_array_push(result, cbor_move(t_item));
        }
    }

    return result;
}

static hegel_basic_gen *list_as_basic(hegel_generator *self)
{
    list_gen_data *d = (list_gen_data *)self->data;

    /* Check if element generator is basic */
    hegel_basic_gen *elem_basic = NULL;
    if (d->elements->vtable.as_basic)
        elem_basic = d->elements->vtable.as_basic(d->elements);

    if (!elem_basic)
        return NULL;

    /* Build list schema:
     * {"type":"list","elements":<elem_schema>,"min_size":min,"max_size":max} */
    static __thread hegel_basic_gen cached;
    static __thread cbor_item_t *cached_schema = NULL;
    static __thread list_basic_ctx *cached_ctx = NULL;

    /* Clean up previous cached schema */
    if (cached_schema) {
        cbor_decref(&cached_schema);
        cached_schema = NULL;
    }
    if (cached_ctx) {
        free(cached_ctx);
        cached_ctx = NULL;
    }

    int nfields = d->unique ? 5 : 4;
    cached_schema = cbor_new_definite_map(nfields);
    if (!cached_schema)
        return NULL;

    cbor_map_add_string(cached_schema, "type", "list");
    cbor_map_add_item(cached_schema, "elements", elem_basic->schema);
    cbor_map_add_int(cached_schema, "min_size", (int64_t)d->min_size);
    cbor_map_add_int(cached_schema, "max_size", (int64_t)d->max_size);
    if (d->unique)
        cbor_map_add_bool(cached_schema, "unique", true);

    cached_ctx = calloc(1, sizeof(list_basic_ctx));
    if (cached_ctx && elem_basic->transform) {
        cached_ctx->elem_transform = elem_basic->transform;
        cached_ctx->elem_ctx = elem_basic->transform_ctx;
    }

    cached.schema = cached_schema;
    if (elem_basic->transform) {
        cached.transform = list_basic_transform;
        cached.transform_ctx = cached_ctx;
    } else {
        cached.transform = NULL;
        cached.transform_ctx = NULL;
    }
    cached.free_ctx = NULL; /* thread-local, not dynamically freed */

    return &cached;
}

/* Compositional draw for non-basic lists */
static void *list_draw(hegel_generator *self, hegel_test_case *tc)
{
    list_gen_data *d = (list_gen_data *)self->data;

    hegel_send_start_span(tc, HEGEL_SPAN_LIST);

    int collection_id = hegel_send_new_collection(tc, d->min_size, d->max_size);
    if (collection_id < 0) {
        hegel_send_stop_span(tc, false);
        return NULL;
    }

    cbor_item_t *result = cbor_new_indefinite_array();
    if (!result) {
        hegel_send_stop_span(tc, false);
        return NULL;
    }

    while (hegel_send_collection_more(tc, collection_id)) {
        hegel_send_start_span(tc, HEGEL_SPAN_LIST_ELEMENT);
        void *elem = hegel_draw_internal(tc, d->elements);
        hegel_send_stop_span(tc, false);

        if (elem) {
            cbor_item_t *item = (cbor_item_t *)elem;
            (void)cbor_array_push(result, cbor_move(item));
        }
    }

    hegel_send_stop_span(tc, false);
    return result;
}

static void list_free(hegel_generator *self)
{
    list_gen_data *d = (list_gen_data *)self->data;
    if (d) {
        hegel_generator_free(d->elements);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable list_vtable = {
    .draw = list_draw,
    .as_basic = list_as_basic,
    .free = list_free
};

hegel_generator *hegel_lists(hegel_generator *elements, size_t min_size, size_t max_size)
{
    if (!elements)
        return NULL;

    list_gen_data *d = calloc(1, sizeof(list_gen_data));
    if (!d)
        return NULL;
    d->elements = elements;
    d->min_size = min_size;
    d->max_size = max_size;
    d->unique = false;

    return hegel_generator_alloc(list_vtable, d);
}

hegel_generator *hegel_lists_unique(hegel_generator *elements, size_t min_size, size_t max_size)
{
    if (!elements)
        return NULL;

    list_gen_data *d = calloc(1, sizeof(list_gen_data));
    if (!d)
        return NULL;
    d->elements = elements;
    d->min_size = min_size;
    d->max_size = max_size;
    d->unique = true;

    return hegel_generator_alloc(list_vtable, d);
}

/* ================================================================
 * Tuples
 * ================================================================ */

typedef struct {
    hegel_generator **elements;
    size_t count;
} tuple_gen_data;

/* Transform for basic tuples: apply each element's transform to its slot */
typedef struct {
    void *(**transforms)(cbor_item_t *, void *);
    void **transform_ctxs;
    size_t count;
} tuple_basic_ctx;

static void *tuple_basic_transform(cbor_item_t *raw, void *ctx)
{
    tuple_basic_ctx *tc = (tuple_basic_ctx *)ctx;

    if (!cbor_isa_array(raw))
        return NULL;

    size_t n = cbor_array_size(raw);
    if (n != tc->count)
        return NULL;

    cbor_item_t *result = cbor_new_definite_array(n);
    if (!result)
        return NULL;

    for (size_t i = 0; i < n; i++) {
        cbor_item_t *elem = cbor_array_get(raw, i);
        if (tc->transforms[i]) {
            void *transformed = tc->transforms[i](elem, tc->transform_ctxs[i]);
            if (transformed) {
                cbor_item_t *t_item = (cbor_item_t *)transformed;
                (void)cbor_array_push(result, cbor_move(t_item));
            }
        } else {
            /* No transform: keep the raw element */
            (void)cbor_array_push(result, elem);
        }
    }

    return result;
}

static hegel_basic_gen *tuple_as_basic(hegel_generator *self)
{
    tuple_gen_data *d = (tuple_gen_data *)self->data;

    /* All elements must be basic */
    hegel_basic_gen **elem_basics = calloc(d->count, sizeof(hegel_basic_gen *));
    if (!elem_basics)
        return NULL;

    for (size_t i = 0; i < d->count; i++) {
        if (d->elements[i]->vtable.as_basic)
            elem_basics[i] = d->elements[i]->vtable.as_basic(d->elements[i]);
        if (!elem_basics[i]) {
            free(elem_basics);
            return NULL;
        }
    }

    /* Build tuple schema: {"type":"tuple","elements":[<schemas>]} */
    static __thread hegel_basic_gen cached;
    static __thread cbor_item_t *cached_schema = NULL;
    static __thread tuple_basic_ctx *cached_ctx = NULL;

    if (cached_schema) {
        cbor_decref(&cached_schema);
        cached_schema = NULL;
    }
    if (cached_ctx) {
        free(cached_ctx->transforms);
        free(cached_ctx->transform_ctxs);
        free(cached_ctx);
        cached_ctx = NULL;
    }

    cbor_item_t *elements_arr = cbor_new_definite_array(d->count);
    if (!elements_arr) {
        free(elem_basics);
        return NULL;
    }
    for (size_t i = 0; i < d->count; i++) {
        cbor_incref(elem_basics[i]->schema);
        (void)cbor_array_push(elements_arr, cbor_move(elem_basics[i]->schema));
    }

    cached_schema = cbor_new_definite_map(2);
    if (!cached_schema) {
        cbor_decref(&elements_arr);
        free(elem_basics);
        return NULL;
    }
    cbor_map_add_string(cached_schema, "type", "tuple");
    cbor_map_add_item(cached_schema, "elements", elements_arr);
    cbor_decref(&elements_arr);

    /* Check if any element has a transform */
    bool has_any_transform = false;
    for (size_t i = 0; i < d->count; i++) {
        if (elem_basics[i]->transform) {
            has_any_transform = true;
            break;
        }
    }

    cached.schema = cached_schema;

    if (has_any_transform) {
        cached_ctx = calloc(1, sizeof(tuple_basic_ctx));
        if (!cached_ctx) {
            free(elem_basics);
            return NULL;
        }
        cached_ctx->count = d->count;
        cached_ctx->transforms = calloc(d->count, sizeof(void *));
        cached_ctx->transform_ctxs = calloc(d->count, sizeof(void *));
        if (!cached_ctx->transforms || !cached_ctx->transform_ctxs) {
            free(cached_ctx->transforms);
            free(cached_ctx->transform_ctxs);
            free(cached_ctx);
            cached_ctx = NULL;
            free(elem_basics);
            return NULL;
        }
        for (size_t i = 0; i < d->count; i++) {
            cached_ctx->transforms[i] = elem_basics[i]->transform;
            cached_ctx->transform_ctxs[i] = elem_basics[i]->transform_ctx;
        }
        cached.transform = tuple_basic_transform;
        cached.transform_ctx = cached_ctx;
    } else {
        cached.transform = NULL;
        cached.transform_ctx = NULL;
    }
    cached.free_ctx = NULL;

    free(elem_basics);
    return &cached;
}

/* Compositional draw for non-basic tuples */
static void *tuple_draw(hegel_generator *self, hegel_test_case *tc)
{
    tuple_gen_data *d = (tuple_gen_data *)self->data;

    hegel_send_start_span(tc, HEGEL_SPAN_TUPLE);

    cbor_item_t *result = cbor_new_definite_array(d->count);
    if (!result) {
        hegel_send_stop_span(tc, false);
        return NULL;
    }

    for (size_t i = 0; i < d->count; i++) {
        void *elem = hegel_draw_internal(tc, d->elements[i]);
        if (elem) {
            cbor_item_t *item = (cbor_item_t *)elem;
            (void)cbor_array_push(result, cbor_move(item));
        }
    }

    hegel_send_stop_span(tc, false);
    return result;
}

static void tuple_free(hegel_generator *self)
{
    tuple_gen_data *d = (tuple_gen_data *)self->data;
    if (d) {
        for (size_t i = 0; i < d->count; i++)
            hegel_generator_free(d->elements[i]);
        free(d->elements);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable tuple_vtable = {
    .draw = tuple_draw,
    .as_basic = tuple_as_basic,
    .free = tuple_free
};

hegel_generator *hegel_tuples(hegel_generator **elements, size_t count)
{
    if (!elements || count == 0)
        return NULL;

    tuple_gen_data *d = calloc(1, sizeof(tuple_gen_data));
    if (!d)
        return NULL;
    d->count = count;
    d->elements = malloc(count * sizeof(hegel_generator *));
    if (!d->elements) {
        free(d);
        return NULL;
    }
    memcpy(d->elements, elements, count * sizeof(hegel_generator *));

    return hegel_generator_alloc(tuple_vtable, d);
}

/* ================================================================
 * Dicts
 * ================================================================ */

typedef struct {
    hegel_generator *keys;
    hegel_generator *values;
    size_t min_size;
    size_t max_size;
} dict_gen_data;

static hegel_basic_gen *dict_as_basic(hegel_generator *self)
{
    dict_gen_data *d = (dict_gen_data *)self->data;

    /* Both keys and values must be basic */
    hegel_basic_gen *key_basic = NULL;
    hegel_basic_gen *val_basic = NULL;

    if (d->keys->vtable.as_basic)
        key_basic = d->keys->vtable.as_basic(d->keys);
    if (d->values->vtable.as_basic)
        val_basic = d->values->vtable.as_basic(d->values);

    if (!key_basic || !val_basic)
        return NULL;

    /* Build dict schema:
     * {"type":"dict","keys":<kschema>,"values":<vschema>,"min_size":min,"max_size":max} */
    static __thread hegel_basic_gen cached;
    static __thread cbor_item_t *cached_schema = NULL;

    if (cached_schema) {
        cbor_decref(&cached_schema);
        cached_schema = NULL;
    }

    cached_schema = cbor_new_definite_map(5);
    if (!cached_schema)
        return NULL;

    cbor_map_add_string(cached_schema, "type", "dict");
    cbor_map_add_item(cached_schema, "keys", key_basic->schema);
    cbor_map_add_item(cached_schema, "values", val_basic->schema);
    cbor_map_add_int(cached_schema, "min_size", (int64_t)d->min_size);
    cbor_map_add_int(cached_schema, "max_size", (int64_t)d->max_size);

    cached.schema = cached_schema;
    /* TODO: compose key/value transforms if needed */
    cached.transform = NULL;
    cached.transform_ctx = NULL;
    cached.free_ctx = NULL;

    return &cached;
}

/* Compositional draw for non-basic dicts */
static void *dict_draw(hegel_generator *self, hegel_test_case *tc)
{
    dict_gen_data *d = (dict_gen_data *)self->data;

    hegel_send_start_span(tc, HEGEL_SPAN_MAP);

    int collection_id = hegel_send_new_collection(tc, d->min_size, d->max_size);
    if (collection_id < 0) {
        hegel_send_stop_span(tc, false);
        return NULL;
    }

    cbor_item_t *result = cbor_new_indefinite_map();
    if (!result) {
        hegel_send_stop_span(tc, false);
        return NULL;
    }

    while (hegel_send_collection_more(tc, collection_id)) {
        hegel_send_start_span(tc, HEGEL_SPAN_MAP_ENTRY);

        void *key = hegel_draw_internal(tc, d->keys);
        void *val = hegel_draw_internal(tc, d->values);

        hegel_send_stop_span(tc, false);

        if (key && val) {
            cbor_item_t *k_item = (cbor_item_t *)key;
            cbor_item_t *v_item = (cbor_item_t *)val;
            struct cbor_pair pair = {.key = cbor_move(k_item), .value = cbor_move(v_item)};
            (void)cbor_map_add(result, pair);
        } else {
            if (key) {
                cbor_item_t *k_item = (cbor_item_t *)key;
                cbor_decref(&k_item);
            }
            if (val) {
                cbor_item_t *v_item = (cbor_item_t *)val;
                cbor_decref(&v_item);
            }
        }
    }

    hegel_send_stop_span(tc, false);
    return result;
}

static void dict_free(hegel_generator *self)
{
    dict_gen_data *d = (dict_gen_data *)self->data;
    if (d) {
        hegel_generator_free(d->keys);
        hegel_generator_free(d->values);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable dict_vtable = {
    .draw = dict_draw,
    .as_basic = dict_as_basic,
    .free = dict_free
};

hegel_generator *hegel_dicts(hegel_generator *keys, hegel_generator *values,
                              size_t min_size, size_t max_size)
{
    if (!keys || !values)
        return NULL;

    dict_gen_data *d = calloc(1, sizeof(dict_gen_data));
    if (!d)
        return NULL;
    d->keys = keys;
    d->values = values;
    d->min_size = min_size;
    d->max_size = max_size;

    return hegel_generator_alloc(dict_vtable, d);
}
