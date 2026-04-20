#include "cbor_helpers.h"
#include "hegel/protocol.h"

#include <string.h>
#include <stdlib.h>

bool cbor_map_add_string(cbor_item_t *map, const char *key, const char *value)
{
    if (!cbor_isa_map(map))
        return false;

    cbor_item_t *k = cbor_build_string(key);
    if (!k)
        return false;
    cbor_item_t *v = cbor_build_string(value);
    if (!v) {
        cbor_decref(&k);
        return false;
    }

    struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
    bool ok = cbor_map_add(map, pair);
    return ok;
}

bool cbor_map_add_int(cbor_item_t *map, const char *key, int64_t value)
{
    if (!cbor_isa_map(map))
        return false;

    cbor_item_t *k = cbor_build_string(key);
    if (!k)
        return false;

    cbor_item_t *v;
    if (value >= 0) {
        v = cbor_build_uint64((uint64_t)value);
    } else {
        /* CBOR negative int: encode -(value + 1) as the uint payload */
        v = cbor_new_int64();
        if (!v) {
            cbor_decref(&k);
            return false;
        }
        cbor_set_uint64(v, (uint64_t)(-(value + 1)));
        cbor_mark_negint(v);
    }
    if (!v) {
        cbor_decref(&k);
        return false;
    }

    struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
    return cbor_map_add(map, pair);
}

bool cbor_map_add_uint(cbor_item_t *map, const char *key, uint32_t value)
{
    if (!cbor_isa_map(map))
        return false;

    cbor_item_t *k = cbor_build_string(key);
    if (!k)
        return false;
    cbor_item_t *v = cbor_build_uint32(value);
    if (!v) {
        cbor_decref(&k);
        return false;
    }

    struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
    return cbor_map_add(map, pair);
}

bool cbor_map_add_bool(cbor_item_t *map, const char *key, bool value)
{
    if (!cbor_isa_map(map))
        return false;

    cbor_item_t *k = cbor_build_string(key);
    if (!k)
        return false;
    cbor_item_t *v = cbor_build_bool(value);
    if (!v) {
        cbor_decref(&k);
        return false;
    }

    struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(v)};
    return cbor_map_add(map, pair);
}

bool cbor_map_add_item(cbor_item_t *map, const char *key, cbor_item_t *value)
{
    if (!cbor_isa_map(map) || !value)
        return false;

    cbor_item_t *k = cbor_build_string(key);
    if (!k)
        return false;

    /* cbor_map_add increfs both key and value via cbor_move semantics,
       but we use cbor_move on key (our temporary) and incref value
       so the caller retains ownership */
    cbor_incref(value);
    struct cbor_pair pair = {.key = cbor_move(k), .value = cbor_move(value)};
    return cbor_map_add(map, pair);
}

cbor_item_t *cbor_map_get(const cbor_item_t *map, const char *key)
{
    if (!map || !cbor_isa_map(map) || !key)
        return NULL;

    size_t n = cbor_map_size(map);
    struct cbor_pair *pairs = cbor_map_handle(map);
    size_t key_len = strlen(key);

    for (size_t i = 0; i < n; i++) {
        cbor_item_t *k = pairs[i].key;
        if (cbor_isa_string(k) && cbor_string_length(k) == key_len &&
            memcmp(cbor_string_handle(k), key, key_len) == 0) {
            return pairs[i].value;
        }
    }
    return NULL;
}

char *cbor_get_string(const cbor_item_t *item)
{
    if (!item)
        return NULL;

    /* Unwrap any CBOR tags (e.g. tag(91) wrapping text as bytestrings) */
    const cbor_item_t *inner = cbor_unwrap_tags(item);
    if (!inner)
        return NULL;

    /* Handle plain text strings (major type 3) */
    if (cbor_isa_string(inner)) {
        size_t len = cbor_string_length(inner);
        char *str = malloc(len + 1);
        if (!str)
            return NULL;
        memcpy(str, cbor_string_handle(inner), len);
        str[len] = '\0';
        return str;
    }

    /* Handle byte strings (major type 2) -- server sends UTF-8 as tagged bytestrings */
    if (cbor_isa_bytestring(inner)) {
        size_t len = cbor_bytestring_length(inner);
        char *str = malloc(len + 1);
        if (!str)
            return NULL;
        memcpy(str, cbor_bytestring_handle(inner), len);
        str[len] = '\0';
        return str;
    }

    return NULL;
}

cbor_item_t *cbor_build_command(const char *command_name)
{
    cbor_item_t *map = cbor_new_definite_map(1);
    if (!map)
        return NULL;

    if (!cbor_map_add_string(map, "command", command_name)) {
        cbor_decref(&map);
        return NULL;
    }
    return map;
}

int cbor_serialize_alloc_checked(cbor_item_t *item, uint8_t **out_buf, size_t *out_len)
{
    if (!item || !out_buf || !out_len)
        return HEGEL_ERR_CBOR;

    unsigned char *buf = NULL;
    size_t buf_size = 0;
    size_t written = cbor_serialize_alloc(item, &buf, &buf_size);
    if (written == 0 || !buf)
        return HEGEL_ERR_CBOR;

    *out_buf = buf;
    *out_len = written;
    return HEGEL_OK;
}
