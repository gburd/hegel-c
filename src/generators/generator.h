#ifndef HEGEL_GENERATOR_INTERNAL_H
#define HEGEL_GENERATOR_INTERNAL_H

#include "hegel/generators.h"
#include <cbor.h>

/* A basic generator has a CBOR schema and optional transform.
 * When transform is NULL, the raw CBOR value from the server is returned as-is. */
typedef struct {
    cbor_item_t *schema;
    void *(*transform)(cbor_item_t *raw_value, void *ctx);
    void *transform_ctx;
    void (*free_ctx)(void *ctx);
} hegel_basic_gen;

/* Generator vtable */
typedef struct {
    /* Draw a value (for non-basic generators, does compositional generation) */
    void *(*draw)(hegel_generator *self, hegel_test_case *tc);
    /* Return basic generator info, or NULL if non-basic */
    hegel_basic_gen *(*as_basic)(hegel_generator *self);
    /* Free the generator */
    void (*free)(hegel_generator *self);
} hegel_gen_vtable;

struct hegel_generator {
    hegel_gen_vtable vtable;
    void *data;
};

/* Internal draw function -- sends generate request or does compositional */
void *hegel_draw_internal(hegel_test_case *tc, hegel_generator *gen);

/* Span labels */
#define HEGEL_SPAN_LIST          1
#define HEGEL_SPAN_LIST_ELEMENT  2
#define HEGEL_SPAN_SET           3
#define HEGEL_SPAN_SET_ELEMENT   4
#define HEGEL_SPAN_MAP           5
#define HEGEL_SPAN_MAP_ENTRY     6
#define HEGEL_SPAN_TUPLE         7
#define HEGEL_SPAN_ONE_OF        8
#define HEGEL_SPAN_OPTIONAL      9
#define HEGEL_SPAN_FIXED_DICT    10
#define HEGEL_SPAN_FLAT_MAP      11
#define HEGEL_SPAN_FILTER        12
#define HEGEL_SPAN_MAPPED        13
#define HEGEL_SPAN_SAMPLED_FROM  14

/* Protocol helpers for generators (send commands on the test case stream) */
void hegel_send_start_span(hegel_test_case *tc, int label);
void hegel_send_stop_span(hegel_test_case *tc, bool discard);
cbor_item_t *hegel_send_generate(hegel_test_case *tc, cbor_item_t *schema);
int hegel_send_new_collection(hegel_test_case *tc, size_t min_size, size_t max_size);
bool hegel_send_collection_more(hegel_test_case *tc, int collection_id);
void hegel_send_collection_reject(hegel_test_case *tc, int collection_id, const char *why);

/* Allocate a generator with the given vtable and data pointer */
hegel_generator *hegel_generator_alloc(hegel_gen_vtable vtable, void *data);

#endif /* HEGEL_GENERATOR_INTERNAL_H */
