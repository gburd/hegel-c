/*
 * Minimal JSON parsing helpers for conformance test binaries.
 *
 * These parse small parameter objects from argv[1] using simple
 * string scanning. Not a general-purpose JSON parser.
 */
#ifndef HEGEL_CONFORMANCE_JSON_HELPERS_H
#define HEGEL_CONFORMANCE_JSON_HELPERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Find a numeric value for a given key in a JSON string.
 * Handles integer and float values. Returns true if found.
 * Works for: "key": 123 or "key": -123 or "key": 1.5
 */
bool json_get_number(const char *json, const char *key,
                     double *out_value);

/*
 * Find an integer value for a given key in a JSON string.
 * Returns true if found.
 */
bool json_get_int(const char *json, const char *key, int64_t *out_value);

/*
 * Find a boolean value for a given key in a JSON string.
 * Returns true if found.
 */
bool json_get_bool(const char *json, const char *key, bool *out_value);

/*
 * Find a string value for a given key in a JSON string.
 * Returns a newly allocated string, or NULL if not found.
 * Caller must free.
 */
char *json_get_string(const char *json, const char *key);

/*
 * Parse a JSON array of strings for a given key.
 * Sets *out_count and returns an array of newly allocated strings.
 * Caller must free each string and the array.
 * Returns NULL if not found or on error.
 */
char **json_get_string_array(const char *json, const char *key,
                             size_t *out_count);

/*
 * Parse a JSON array of objects for a given key.
 * Returns an array of newly allocated strings, each containing the raw
 * JSON text of one array element (e.g. "{\"min_value\": 0, \"max_value\": 100}").
 * Sets *out_count to the number of elements.
 * Caller must free each string and the array.
 * Returns NULL if not found or on error.
 */
char **json_get_object_array(const char *json, const char *key,
                             size_t *out_count);

/*
 * Parse a JSON array of integers for a given key.
 * Sets *out_count and returns a heap-allocated array.
 * Caller must free the array.
 * Returns NULL if not found or on error.
 */
int64_t *json_get_int_array(const char *json, const char *key,
                             size_t *out_count);

/*
 * Check if a key's value is JSON null.
 * Returns true if the key exists and has value "null".
 */
bool json_is_null(const char *json, const char *key);

#endif /* HEGEL_CONFORMANCE_JSON_HELPERS_H */
