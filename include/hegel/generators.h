/*
 * hegel/generators.h - Generator constructors and combinators.
 *
 * Ownership rule: generators passed to combinators (map, filter, flat_map,
 * one_of, lists, tuples, dicts, optional) are CONSUMED. The combinator takes
 * ownership and will free the source generator. Do not use or free a generator
 * after passing it to a combinator.
 */
#ifndef HEGEL_GENERATORS_H
#define HEGEL_GENERATORS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hegel/types.h"

/* Free a generator. Only call on generators you still own (not passed to a combinator). */
void hegel_generator_free(hegel_generator *gen);

/* === Primitive Generators (all basic) === */

/* Integers in [min, max] */
hegel_generator *hegel_integers(int64_t min_value, int64_t max_value);

/* Floating point in [min, max] with options */
hegel_generator *hegel_floats(double min_value, double max_value);
hegel_generator *hegel_floats_ex(double min_value, double max_value,
                                  bool exclude_min, bool exclude_max,
                                  bool allow_nan, bool allow_infinity, int width);

/* Null values */
hegel_generator *hegel_nulls(void);

/* Booleans with optional probability */
hegel_generator *hegel_booleans(void);
hegel_generator *hegel_booleans_p(double p);

/*
 * Text (UTF-8 strings). min_size and max_size are in Unicode codepoints,
 * not bytes, following JSON Schema convention.
 */
hegel_generator *hegel_text(size_t min_size, size_t max_size);

/*
 * Text with full Unicode filtering parameters.
 * min_size and max_size are in Unicode codepoints, not bytes, matching
 * JSON Schema convention (same as hegel_text()).
 * All pointer parameters are optional (NULL to omit).
 * categories and exclude_categories are mutually exclusive.
 *
 * codec             - Restrict to characters encodable in this codec (e.g. "ascii", "utf-8").
 * min_codepoint     - Minimum Unicode codepoint (0 to omit).
 * max_codepoint     - Maximum Unicode codepoint (0 to omit).
 * categories        - NULL-terminated array of Unicode general categories to include (e.g. {"L","Nd",NULL}).
 * exclude_categories - NULL-terminated array of Unicode general categories to exclude.
 * include_characters - Always include these specific characters even if excluded by other filters.
 * exclude_characters - Always exclude these specific characters.
 */
hegel_generator *hegel_text_ex(size_t min_size, size_t max_size,
                                const char *codec,
                                uint32_t min_codepoint, uint32_t max_codepoint,
                                const char **categories,
                                const char **exclude_categories,
                                const char *include_characters,
                                const char *exclude_characters);

/*
 * Single Unicode character generator. Equivalent to hegel_text_ex(1, 1, ...).
 * All pointer parameters are optional (NULL to omit).
 */
hegel_generator *hegel_characters(const char *codec,
                                   uint32_t min_codepoint, uint32_t max_codepoint,
                                   const char **categories,
                                   const char **exclude_categories,
                                   const char *include_characters,
                                   const char *exclude_characters);

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
/* Sample from array of float values */
hegel_generator *hegel_sampled_from_floats(const double *values, size_t count);
/* Sample from array of bool values */
hegel_generator *hegel_sampled_from_bools(const bool *values, size_t count);

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
hegel_generator *hegel_uuids(void);               /* any version */
hegel_generator *hegel_uuids_version(int version); /* specific version (1-5) */

/* === Collection Generators === */

/* Lists of elements with size bounds */
hegel_generator *hegel_lists(hegel_generator *elements, size_t min_size, size_t max_size);
hegel_generator *hegel_lists_unique(hegel_generator *elements, size_t min_size, size_t max_size);

/* Arrays: alias for hegel_lists */
static inline hegel_generator *hegel_arrays(hegel_generator *elements,
                                             size_t min_size, size_t max_size)
{
    return hegel_lists(elements, min_size, max_size);
}

/* Fixed-size tuples */
hegel_generator *hegel_tuples(hegel_generator **elements, size_t count);

/* Dictionaries (key-value maps) */
hegel_generator *hegel_dicts(hegel_generator *keys, hegel_generator *values,
                              size_t min_size, size_t max_size);

/* === Combinators === */

/*
 * Map: transform generator output. Consumes source.
 * If the source is basic (schema-based), the mapped generator preserves the
 * schema and fn receives a cbor_item_t*. free_fn is called on the transformed
 * value when it is no longer needed (may be NULL if not needed).
 */
typedef void *(*hegel_map_fn)(void *value, void *ctx);
typedef void (*hegel_free_fn)(void *value);
hegel_generator *hegel_map(hegel_generator *source, hegel_map_fn fn, void *ctx,
                            hegel_free_fn free_fn);

/*
 * FlatMap: generate a new generator from a drawn value. Always non-basic.
 * Consumes source. fn receives the source value and must return a freshly
 * allocated generator.
 */
typedef hegel_generator *(*hegel_flatmap_fn)(void *value, void *ctx);
hegel_generator *hegel_flat_map(hegel_generator *source, hegel_flatmap_fn fn, void *ctx);

/*
 * Filter: keep values matching predicate. Always non-basic. Consumes source.
 * Retries up to 3 times; if all fail, calls hegel_assume(false) to mark the
 * test case as INVALID.
 */
typedef bool (*hegel_predicate_fn)(void *value, void *ctx);
hegel_generator *hegel_filter(hegel_generator *source, hegel_predicate_fn pred, void *ctx);

/* One-of: choose from multiple generators */
hegel_generator *hegel_one_of(hegel_generator **generators, size_t count);

/* Optional: nullable version of a generator */
hegel_generator *hegel_optional(hegel_generator *element);

#endif /* HEGEL_GENERATORS_H */
