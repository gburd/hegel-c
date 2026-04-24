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

    char *end = NULL;
    double v = strtod(val, &end);
    if (end == val)
        return false;

    *out_value = v;
    return true;
}

bool json_get_int(const char *json, const char *key, int64_t *out_value)
{
    double v;
    if (!json_get_number(json, key, &v))
        return false;
    *out_value = (int64_t)v;
    return true;
}

bool json_get_bool(const char *json, const char *key, bool *out_value)
{
    const char *val = find_key_value(json, key);
    if (!val)
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

char *json_get_string(const char *json, const char *key)
{
    const char *val = find_key_value(json, key);
    if (!val || *val != '"')
        return NULL;

    val++; /* skip opening quote */
    const char *end = strchr(val, '"');
    if (!end)
        return NULL;

    size_t len = (size_t)(end - val);
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    memcpy(result, val, len);
    result[len] = '\0';
    return result;
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
