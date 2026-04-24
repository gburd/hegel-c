/*
 * hegel/hegel.h - Main public API for Hegel property-based testing.
 *
 * Typical usage: create a session, define a test function that draws values
 * from generators and asserts properties, then call hegel_run_test().
 */
#ifndef HEGEL_H
#define HEGEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hegel/types.h"

/* Test result status */
typedef enum {
    HEGEL_STATUS_VALID,
    HEGEL_STATUS_INVALID,
    HEGEL_STATUS_INTERESTING
} hegel_status;

/*
 * Test results returned by value from hegel_run_test().
 *
 * This struct is typically stack-allocated (returned by value), so there is no
 * need to free the struct itself. However, it owns heap-allocated strings
 * (`seed` and `error`) that must be released by calling hegel_results_free().
 *
 * Usage:
 *   hegel_results r = hegel_run_test(session, my_test, NULL, &settings);
 *   // ... inspect r.passed, r.seed, r.error, etc. ...
 *   hegel_results_free(&r);   // frees r.seed and r.error, not &r
 *
 * If `passed` is true, `error` will be NULL.
 */
typedef struct {
    bool passed;
    int interesting_test_cases;
    int valid_test_cases;
    int invalid_test_cases;
    int total_test_cases;
    char *seed;       /* heap-allocated, freed by hegel_results_free() */
    char *error;      /* NULL if passed, freed by hegel_results_free() */
} hegel_results;

/*
 * Settings for test execution. Use HEGEL_DEFAULT_SETTINGS for defaults.
 *
 * max_examples  - Maximum number of valid test cases to run (default 100).
 * seed          - RNG seed. 0 means use a random seed.
 * derandomize   - If true, derive seed from the test identity for reproducibility.
 * database      - Path to the example database, or NULL for the default location.
 * database_key  - Key within the database, or NULL to derive automatically.
 */
typedef struct {
    int max_examples;          /* default 100 */
    uint64_t seed;             /* 0 = random */
    bool derandomize;
    const char *database;      /* NULL = default */
    const char *database_key;  /* NULL = auto */
} hegel_settings;

#define HEGEL_DEFAULT_SETTINGS { .max_examples = 100 }

/* Session management */
hegel_session *hegel_session_new(void);
void hegel_session_free(hegel_session *s);

/* Test function signature -- user implements this */
typedef void (*hegel_test_fn)(hegel_test_case *tc, void *user_data);

/* Run a property test */
hegel_results hegel_run_test(hegel_session *s, hegel_test_fn fn, void *user_data,
                              const hegel_settings *settings);

/*
 * Free the owned resources inside a hegel_results struct (seed and error
 * strings). This does NOT free the struct itself, since it is typically
 * stack-allocated (returned by value from hegel_run_test).
 */
void hegel_results_free(hegel_results *r);

/*
 * In-test functions. These use thread-local state and must only be called
 * from within a hegel test function body.
 */

/*
 * Precondition filter. If condition is false, marks the current test case as
 * INVALID and aborts execution via longjmp. Do not call outside a test body.
 */
void hegel_assume(bool condition);

/*
 * Mark the current test case as INTERESTING (i.e., a counterexample was found).
 * The server will shrink this test case to find a minimal reproduction.
 * Aborts execution via longjmp. Do not call outside a test body.
 */
void hegel_fail(const char *message);

/* Send a target observation to the server for guided optimization. */
void hegel_target(double value, const char *label);

/* Print a message during the final (shrunk) replay only; silent otherwise. */
void hegel_note(const char *message);

/* Draw a value from a generator (typed wrappers) */
int64_t hegel_draw_int(hegel_test_case *tc, hegel_generator *gen);
double hegel_draw_float(hegel_test_case *tc, hegel_generator *gen);
bool hegel_draw_bool(hegel_test_case *tc, hegel_generator *gen);

/* Returns a heap-allocated UTF-8 string. Caller must free() it. */
char *hegel_draw_string(hegel_test_case *tc, hegel_generator *gen);

/* Returns a heap-allocated buffer. Caller must free() it. Length written to *out_len. */
uint8_t *hegel_draw_bytes(hegel_test_case *tc, hegel_generator *gen,
                           size_t *out_len);

/* Generic draw (returns cbor_item_t*, caller decref's) */
void *hegel_draw_raw(hegel_test_case *tc, hegel_generator *gen);

#endif /* HEGEL_H */
