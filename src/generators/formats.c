#include "generator.h"
#include "../protocol/cbor_helpers.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Basic generator helper (same pattern as primitives.c)
 * ================================================================ */

typedef struct {
    hegel_basic_gen basic;
} fmt_gen_data;

static hegel_basic_gen *fmt_as_basic(hegel_generator *self)
{
    fmt_gen_data *d = (fmt_gen_data *)self->data;
    return &d->basic;
}

static void fmt_free(hegel_generator *self)
{
    fmt_gen_data *d = (fmt_gen_data *)self->data;
    if (d) {
        if (d->basic.schema)
            cbor_decref(&d->basic.schema);
        free(d);
    }
    free(self);
}

static hegel_gen_vtable fmt_vtable = {
    .draw = NULL,
    .as_basic = fmt_as_basic,
    .free = fmt_free
};

static hegel_generator *make_fmt_gen(cbor_item_t *schema)
{
    if (!schema)
        return NULL;

    fmt_gen_data *d = calloc(1, sizeof(fmt_gen_data));
    if (!d) {
        cbor_decref(&schema);
        return NULL;
    }
    d->basic.schema = schema;
    d->basic.transform = NULL;
    d->basic.transform_ctx = NULL;
    d->basic.free_ctx = NULL;

    hegel_generator *gen = hegel_generator_alloc(fmt_vtable, d);
    if (!gen) {
        cbor_decref(&schema);
        free(d);
        return NULL;
    }
    return gen;
}

/* ================================================================
 * Format generators
 * Each is a basic generator with a simple {"type": <name>} schema.
 * ================================================================ */

static hegel_generator *make_format_gen(const char *type_name)
{
    cbor_item_t *schema = cbor_new_definite_map(1);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", type_name);
    return make_fmt_gen(schema);
}

hegel_generator *hegel_emails(void)    { return make_format_gen("email"); }
hegel_generator *hegel_urls(void)      { return make_format_gen("url"); }
hegel_generator *hegel_domains(void)   { return make_format_gen("domain"); }
hegel_generator *hegel_dates(void)     { return make_format_gen("date"); }
hegel_generator *hegel_times(void)     { return make_format_gen("time"); }
hegel_generator *hegel_datetimes(void) { return make_format_gen("datetime"); }

/* IP address generators use {"type": "ip_address", "version": N} */
hegel_generator *hegel_ip4_addresses(void)
{
    cbor_item_t *schema = cbor_new_definite_map(2);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "ip_address");
    cbor_map_add_int(schema, "version", 4);
    return make_fmt_gen(schema);
}

hegel_generator *hegel_ip6_addresses(void)
{
    cbor_item_t *schema = cbor_new_definite_map(2);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "ip_address");
    cbor_map_add_int(schema, "version", 6);
    return make_fmt_gen(schema);
}

hegel_generator *hegel_ip_addresses(void)
{
    /* one_of(ip4, ip6) — uses the one_of combinator */
    hegel_generator *ip4 = hegel_ip4_addresses();
    if (!ip4)
        return NULL;
    hegel_generator *ip6 = hegel_ip6_addresses();
    if (!ip6) {
        hegel_generator_free(ip4);
        return NULL;
    }
    hegel_generator *gens[2] = { ip4, ip6 };
    hegel_generator *result = hegel_one_of(gens, 2);
    if (!result) {
        hegel_generator_free(ip4);
        hegel_generator_free(ip6);
        return NULL;
    }
    return result;
}

/* ================================================================
 * Regex
 * ================================================================ */

hegel_generator *hegel_from_regex(const char *pattern, bool fullmatch)
{
    if (!pattern)
        return NULL;

    /* Schema: {"type": "regex", "pattern": pattern, "fullmatch": fullmatch} */
    cbor_item_t *schema = cbor_new_definite_map(3);
    if (!schema)
        return NULL;
    cbor_map_add_string(schema, "type", "regex");
    cbor_map_add_string(schema, "pattern", pattern);
    cbor_map_add_bool(schema, "fullmatch", fullmatch);

    return make_fmt_gen(schema);
}
