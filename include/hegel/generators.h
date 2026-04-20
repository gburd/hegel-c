#ifndef HEGEL_GENERATORS_H
#define HEGEL_GENERATORS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hegel/types.h"

/* Generator lifecycle */
void hegel_generator_free(hegel_generator *gen);

/* === Primitive Generators (all basic) === */

/* Integers in [min, max] */
hegel_generator *hegel_integers(int64_t min_value, int64_t max_value);

/* Floating point in [min, max] with options */
hegel_generator *hegel_floats(double min_value, double max_value);
hegel_generator *hegel_floats_ex(double min_value, double max_value,
                                  bool exclude_min, bool exclude_max,
                                  bool allow_nan, bool allow_infinity, int width);

/* Booleans with optional probability */
hegel_generator *hegel_booleans(void);
hegel_generator *hegel_booleans_p(double p);

/* Text (UTF-8 strings) with size bounds (codepoint count) */
hegel_generator *hegel_text(size_t min_size, size_t max_size);

/* Binary data with size bounds (byte count) */
hegel_generator *hegel_binary(size_t min_size, size_t max_size);

/* Constant value generators */
hegel_generator *hegel_just_null(void);
hegel_generator *hegel_just_int(int64_t value);
hegel_generator *hegel_just_float(double value);
hegel_generator *hegel_just_string(const char *value);
hegel_generator *hegel_just_bool(bool value);

/* Sample from array of string values */
hegel_generator *hegel_sampled_from_strings(const char **values, size_t count);
/* Sample from array of int values */
hegel_generator *hegel_sampled_from_ints(const int64_t *values, size_t count);

/* Regular expression */
hegel_generator *hegel_from_regex(const char *pattern, bool fullmatch);

/* === Format Generators === */
hegel_generator *hegel_emails(void);
hegel_generator *hegel_urls(void);
hegel_generator *hegel_domains(void);
hegel_generator *hegel_ip4_addresses(void);
hegel_generator *hegel_ip6_addresses(void);
hegel_generator *hegel_ip_addresses(void);  /* v4 or v6 */
hegel_generator *hegel_dates(void);
hegel_generator *hegel_times(void);
hegel_generator *hegel_datetimes(void);

/* === Collection Generators === */

/* Lists of elements with size bounds */
hegel_generator *hegel_lists(hegel_generator *elements, size_t min_size, size_t max_size);
hegel_generator *hegel_lists_unique(hegel_generator *elements, size_t min_size, size_t max_size);

/* Fixed-size tuples */
hegel_generator *hegel_tuples(hegel_generator **elements, size_t count);

/* Dictionaries (key-value maps) */
hegel_generator *hegel_dicts(hegel_generator *keys, hegel_generator *values,
                              size_t min_size, size_t max_size);

/* === Combinators === */

/* Map: transform output (PRESERVES schema if source is basic) */
typedef void *(*hegel_map_fn)(void *value, void *ctx);
typedef void (*hegel_free_fn)(void *value);
hegel_generator *hegel_map(hegel_generator *source, hegel_map_fn fn, void *ctx,
                            hegel_free_fn free_fn);

/* FlatMap: generate a generator from a value (always non-basic) */
typedef hegel_generator *(*hegel_flatmap_fn)(void *value, void *ctx);
hegel_generator *hegel_flat_map(hegel_generator *source, hegel_flatmap_fn fn, void *ctx);

/* Filter: keep values matching predicate (3 retries then assume(false)) */
typedef bool (*hegel_predicate_fn)(void *value, void *ctx);
hegel_generator *hegel_filter(hegel_generator *source, hegel_predicate_fn pred, void *ctx);

/* One-of: choose from multiple generators */
hegel_generator *hegel_one_of(hegel_generator **generators, size_t count);

/* Optional: nullable version of a generator */
hegel_generator *hegel_optional(hegel_generator *element);

#endif /* HEGEL_GENERATORS_H */
