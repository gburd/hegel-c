/*
 * Minimal JSON parsing for conformance test parameters.
 */
#include "json_helpers.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Find the value start position after "key": in the JSON string.
 * Returns pointer to the first non-whitespace character of the value,
 * or NULL if not found.
 */
static const char *find_key_value(const char *json, const char *key)
{
    if (!json || !key)
        return NULL;

    size_t key_len = strlen(key);
    const char *p = json;

    while (*p) {
        /* Look for a quote that starts the key */
        const char *q = strchr(p, '"');
        if (!q)
            return NULL;
        q++; /* skip opening quote */

        /* Check if this matches our key */
        if (strncmp(q, key, key_len) == 0 && q[key_len] == '"') {
            /* Found the key. Skip past "key" and find the colon. */
            const char *after_key = q + key_len + 1;
            while (*after_key && isspace((unsigned char)*after_key))
                after_key++;
            if (*after_key != ':')
                { p = after_key; continue; }
            after_key++; /* skip colon */
            while (*after_key && isspace((unsigned char)*after_key))
                after_key++;
            return after_key;
        }
        p = q + key_len; /* move past this key attempt */
        /* Safely advance past the closing quote */
        const char *end_q = strchr(q, '"');
        if (end_q)
            p = end_q + 1;
    }
    return NULL;
}

bool json_get_number(const char *json, const char *key, double *out_value)
{
    const char *val = find_key_value(json, key);
    if (!val)
        return false;

    /* null means "not set" */
    if (strncmp(val, "null", 4) == 0)
        return false;

    char *end = NULL;
    double v = strtod(val, &end);
    if (end == val)
        return false;

    *out_value = v;
    return true;
}

bool json_get_int(const char *json, const char *key, int64_t *out_value)
{
    const char *val = find_key_value(json, key);
    if (!val)
        return false;

    /* null means "not set" */
    if (strncmp(val, "null", 4) == 0)
        return false;

    char *end = NULL;
    long long v = strtoll(val, &end, 10);
    if (end == val)
        return false;

    *out_value = (int64_t)v;
    return true;
}

bool json_get_bool(const char *json, const char *key, bool *out_value)
{
    const char *val = find_key_value(json, key);
    if (!val)
        return false;

    /* null means "not set" */
    if (strncmp(val, "null", 4) == 0)
        return false;

    if (strncmp(val, "true", 4) == 0) {
        *out_value = true;
        return true;
    }
    if (strncmp(val, "false", 5) == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

bool json_is_null(const char *json, const char *key)
{
    const char *val = find_key_value(json, key);
    if (!val)
        return false;
    return strncmp(val, "null", 4) == 0;
}

/*
 * Find the end of a JSON string value, handling escape sequences.
 * Starts at the first character inside the quotes (after opening quote).
 * Returns pointer to the closing quote, or NULL if malformed.
 */
static const char *find_json_string_end(const char *p)
{
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++; /* skip escaped character */
            if (*p == '\0')
                return NULL;
        }
        p++;
    }
    return *p == '"' ? p : NULL;
}

/*
 * Encode a Unicode codepoint as UTF-8 into the buffer.
 * Returns the number of bytes written (1-4), or 0 on error.
 */
static size_t encode_utf8(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static uint16_t parse_hex4(const char *p)
{
    uint16_t val = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex_digit(p[i]);
        if (d < 0) return 0;
        val = (uint16_t)((val << 4) | (uint16_t)d);
    }
    return val;
}

/*
 * Decode a JSON string value with escape sequences into a UTF-8 string.
 * src points to the first character after the opening quote.
 * src_end points to the closing quote.
 * Returns a newly allocated UTF-8 string. Caller must free.
 */
static char *json_unescape(const char *src, const char *src_end)
{
    size_t max_len = (size_t)(src_end - src) * 4 + 1; /* worst case: all 4-byte UTF-8 */
    char *result = malloc(max_len);
    if (!result) return NULL;
    size_t out = 0;

    const char *p = src;
    while (p < src_end) {
        if (*p != '\\') {
            result[out++] = *p++;
            continue;
        }
        p++; /* skip backslash */
        if (p >= src_end) break;
        switch (*p) {
        case '"':  result[out++] = '"'; p++; break;
        case '\\': result[out++] = '\\'; p++; break;
        case '/':  result[out++] = '/'; p++; break;
        case 'b':  result[out++] = '\b'; p++; break;
        case 'f':  result[out++] = '\f'; p++; break;
        case 'n':  result[out++] = '\n'; p++; break;
        case 'r':  result[out++] = '\r'; p++; break;
        case 't':  result[out++] = '\t'; p++; break;
        case 'u': {
            p++; /* skip 'u' */
            if (p + 4 > src_end) break;
            uint16_t hi = parse_hex4(p);
            p += 4;
            uint32_t cp = hi;

            /* Handle UTF-16 surrogate pairs */
            if (hi >= 0xD800 && hi <= 0xDBFF && p + 6 <= src_end &&
                p[0] == '\\' && p[1] == 'u') {
                uint16_t lo = parse_hex4(p + 2);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((uint32_t)(hi - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
            }

            size_t n = encode_utf8(cp, result + out);
            out += n;
            break;
        }
        default:
            result[out++] = *p++;
            break;
        }
    }

    result[out] = '\0';
    return result;
}

char *json_get_string(const char *json, const char *key)
{
    const char *val = find_key_value(json, key);
    if (!val || *val != '"')
        return NULL;

    val++; /* skip opening quote */
    const char *end = find_json_string_end(val);
    if (!end)
        return NULL;

    return json_unescape(val, end);
}

char **json_get_string_array(const char *json, const char *key,
                             size_t *out_count)
{
    const char *val = find_key_value(json, key);
    if (!val || *val != '[')
        return NULL;

    val++; /* skip '[' */

    /* Count strings first */
    size_t count = 0;
    size_t cap = 8;
    char **results = malloc(cap * sizeof(char *));
    if (!results)
        return NULL;

    const char *p = val;
    while (*p && *p != ']') {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == ']')
            break;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') {
            p++;
            continue;
        }

        p++; /* skip opening quote */
        const char *end = strchr(p, '"');
        if (!end) {
            /* Malformed -- free and bail */
            for (size_t i = 0; i < count; i++)
                free(results[i]);
            free(results);
            return NULL;
        }

        size_t slen = (size_t)(end - p);
        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(results, cap * sizeof(char *));
            if (!tmp) {
                for (size_t i = 0; i < count; i++)
                    free(results[i]);
                free(results);
                return NULL;
            }
            results = tmp;
        }
        results[count] = malloc(slen + 1);
        if (!results[count]) {
            for (size_t i = 0; i < count; i++)
                free(results[i]);
            free(results);
            return NULL;
        }
        memcpy(results[count], p, slen);
        results[count][slen] = '\0';
        count++;

        p = end + 1; /* skip closing quote */
    }

    *out_count = count;
    return results;
}

int64_t *json_get_int_array(const char *json, const char *key,
                             size_t *out_count)
{
    const char *val = find_key_value(json, key);
    if (!val || *val != '[')
        return NULL;

    val++; /* skip '[' */

    size_t count = 0;
    size_t cap = 8;
    int64_t *results = malloc(cap * sizeof(int64_t));
    if (!results)
        return NULL;

    const char *p = val;
    while (*p && *p != ']') {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == ']')
            break;
        if (*p == ',') {
            p++;
            continue;
        }

        /* Parse an integer (possibly negative) */
        char *end = NULL;
        long long v = strtoll(p, &end, 10);
        if (end == p) {
            /* Not a number -- skip character */
            p++;
            continue;
        }

        if (count >= cap) {
            cap *= 2;
            int64_t *tmp = realloc(results, cap * sizeof(int64_t));
            if (!tmp) {
                free(results);
                return NULL;
            }
            results = tmp;
        }
        results[count++] = (int64_t)v;
        p = end;
    }

    *out_count = count;
    return results;
}

char **json_get_object_array(const char *json, const char *key,
                             size_t *out_count)
{
    const char *val = find_key_value(json, key);
    if (!val || *val != '[')
        return NULL;

    val++; /* skip '[' */

    size_t count = 0;
    size_t cap = 8;
    char **results = malloc(cap * sizeof(char *));
    if (!results)
        return NULL;

    const char *p = val;
    while (*p && *p != ']') {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == ']')
            break;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '{') {
            p++;
            continue;
        }

        /* Find the matching closing brace (simple nesting) */
        const char *start = p;
        int depth = 0;
        do {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            else if (*p == '"') {
                /* Skip over string contents to avoid counting braces in strings */
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\') p++; /* skip escaped char */
                    if (*p) p++;
                }
            }
            if (*p) p++;
        } while (*p && depth > 0);

        size_t olen = (size_t)(p - start);
        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(results, cap * sizeof(char *));
            if (!tmp) {
                for (size_t i = 0; i < count; i++)
                    free(results[i]);
                free(results);
                return NULL;
            }
            results = tmp;
        }
        results[count] = malloc(olen + 1);
        if (!results[count]) {
            for (size_t i = 0; i < count; i++)
                free(results[i]);
            free(results);
            return NULL;
        }
        memcpy(results[count], start, olen);
        results[count][olen] = '\0';
        count++;
    }

    *out_count = count;
    return results;
}
