#ifndef HEGEL_PROTOCOL_CBOR_HELPERS_H
#define HEGEL_PROTOCOL_CBOR_HELPERS_H

#include <cbor.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Unwrap CBOR tags to get the inner content item.
 * The server (Python/cbor2) may wrap values in CBOR tags (e.g. tag(91) for
 * text as bytestrings). This helper peels all tag layers to reach the
 * underlying data item and returns a BORROWED pointer (the returned item is
 * still owned by its enclosing tag; do not decref it).
 *
 * NOTE: libcbor's cbor_tag_item() INCREMENTS the tagged item's refcount.
 * We immediately drop that extra reference so this stays a pure borrow --
 * the enclosing tag still holds a reference, so the inner item remains alive.
 * Without this, every tag unwrap (e.g. every tagged-bytestring text draw)
 * leaks one reference to the inner item.
 */
static inline const cbor_item_t *cbor_unwrap_tags(const cbor_item_t *item)
{
    while (item && cbor_isa_tag(item)) {
        cbor_item_t *inner = cbor_tag_item(item); /* +1 ref */
        cbor_item_t *drop = inner;
        cbor_decref(&drop); /* back to a borrow; the tag keeps inner alive */
        item = inner;
    }
    return item;
}

/*
 * Add a string key-value pair to a CBOR definite map.
 * Returns true on success.
 */
bool cbor_map_add_string(cbor_item_t *map, const char *key, const char *value);

/*
 * Add a string key with integer value to a CBOR definite map.
 * Returns true on success.
 */
bool cbor_map_add_int(cbor_item_t *map, const char *key, int64_t value);

/*
 * Add a string key with uint32 value to a CBOR definite map.
 * Returns true on success.
 */
bool cbor_map_add_uint(cbor_item_t *map, const char *key, uint32_t value);

/*
 * Add a string key with boolean value to a CBOR definite map.
 * Returns true on success.
 */
bool cbor_map_add_bool(cbor_item_t *map, const char *key, bool value);

/*
 * Add a string key with an existing cbor_item_t value to a CBOR definite map.
 * The value's refcount is incremented by the map, so the caller may decref
 * their reference after this call.
 * Returns true on success.
 */
bool cbor_map_add_item(cbor_item_t *map, const char *key, cbor_item_t *value);

/*
 * Look up a string key in a CBOR map.
 * Returns the value item (not incref'd -- caller must NOT decref unless they incref first),
 * or NULL if not found.
 */
cbor_item_t *cbor_map_get(const cbor_item_t *map, const char *key);

/*
 * Get a string value from a CBOR item. Returns a newly allocated string
 * that the caller must free(), or NULL if the item is not a string.
 */
char *cbor_get_string(const cbor_item_t *item);

/*
 * Safely read an unsigned integer from a CBOR uint item, regardless of width.
 * libcbor's cbor_get_uint64() is only valid for CBOR_INT_64 width items;
 * calling it on narrower items is undefined behavior.
 */
static inline uint64_t cbor_get_uint_value(const cbor_item_t *item)
{
    switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:  return cbor_get_uint8(item);
    case CBOR_INT_16: return cbor_get_uint16(item);
    case CBOR_INT_32: return cbor_get_uint32(item);
    case CBOR_INT_64: return cbor_get_uint64(item);
    }
    return 0;
}

/*
 * Safely read a signed integer from a CBOR int item (uint or negint).
 */
static inline int64_t cbor_get_int_value(const cbor_item_t *item)
{
    if (cbor_isa_uint(item))
        return (int64_t)cbor_get_uint_value(item);
    if (cbor_isa_negint(item))
        return -1 - (int64_t)cbor_get_uint_value(item);
    return 0;
}

/*
 * Build a CBOR command map: {"command": command_name}.
 * Returns a new definite map with 1 pair allocated, or NULL on failure.
 * Caller owns the returned item.
 */
cbor_item_t *cbor_build_command(const char *command_name);

/*
 * Serialize a CBOR item to a newly allocated byte buffer.
 * Sets *out_buf and *out_len. Caller must free *out_buf.
 * Returns HEGEL_OK on success, HEGEL_ERR_CBOR on failure.
 */
int cbor_serialize_alloc_checked(cbor_item_t *item, uint8_t **out_buf, size_t *out_len);

#endif /* HEGEL_PROTOCOL_CBOR_HELPERS_H */
