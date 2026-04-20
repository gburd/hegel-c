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

/* Test results */
typedef struct {
    bool passed;
    int interesting_test_cases;
    int valid_test_cases;
    int invalid_test_cases;
    int total_test_cases;
    char *seed;       /* caller must free */
    char *error;      /* NULL if passed, caller must free */
} hegel_results;

/* Settings for test execution */
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
void hegel_results_free(hegel_results *r);

/* In-test functions (use thread-local test case state) */
void hegel_assume(bool condition);
void hegel_target(double value, const char *label);
void hegel_note(const char *message);

/* Draw a value from a generator (typed wrappers) */
int64_t hegel_draw_int(hegel_test_case *tc, hegel_generator *gen);
double hegel_draw_float(hegel_test_case *tc, hegel_generator *gen);
bool hegel_draw_bool(hegel_test_case *tc, hegel_generator *gen);
char *hegel_draw_string(hegel_test_case *tc, hegel_generator *gen);      /* caller frees */
uint8_t *hegel_draw_bytes(hegel_test_case *tc, hegel_generator *gen,
                           size_t *out_len);                              /* caller frees */

/* Generic draw (returns cbor_item_t*, caller decref's) */
void *hegel_draw_raw(hegel_test_case *tc, hegel_generator *gen);

#endif /* HEGEL_H */
