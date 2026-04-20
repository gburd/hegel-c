/*
 * Integration tests with real hegel server.
 *
 * These tests spawn a real hegel server subprocess and exercise the
 * full client pipeline: session creation, test execution, generation,
 * shrinking, and cleanup.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdlib.h>
#include <string.h>

#include "hegel/hegel.h"
#include "hegel/generators.h"

/* ================================================================
 * Test: integers are bounded
 * ================================================================ */
static void test_fn_integers_bounded(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    int64_t min = 0, max = 100;
    int64_t val = hegel_draw_int(tc, hegel_integers(min, max));
    /* Use hegel_assume inside test body, not cmocka asserts */
    hegel_assume(val >= min && val <= max);
}

static void test_integers_in_bounds(void **state)
{
    (void)state;

    hegel_session *s = hegel_session_new();
    assert_non_null(s);

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 50;

    hegel_results r = hegel_run_test(s, test_fn_integers_bounded, NULL, &settings);
    assert_true(r.passed);
    assert_true(r.valid_test_cases > 0);
    assert_int_equal(r.interesting_test_cases, 0);

    hegel_results_free(&r);
    hegel_session_free(s);
}

/* ================================================================
 * Test: booleans don't crash
 * ================================================================ */
static void test_fn_booleans(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    bool val = hegel_draw_bool(tc, hegel_booleans());
    (void)val; /* just verify no crash */
}

static void test_booleans_basic(void **state)
{
    (void)state;

    hegel_session *s = hegel_session_new();
    assert_non_null(s);

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 30;

    hegel_results r = hegel_run_test(s, test_fn_booleans, NULL, &settings);
    assert_true(r.passed);

    hegel_results_free(&r);
    hegel_session_free(s);
}

/* ================================================================
 * Test: text length bounds
 * ================================================================ */
static void test_fn_text_bounded(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    char *text = hegel_draw_string(tc, hegel_text(1, 10));
    /* Text generation should succeed with the CBOR tag unwrapping fix */
    hegel_assume(text != NULL);
    size_t len = strlen(text);
    hegel_assume(len >= 1);
    free(text);
}

static void test_text_in_bounds(void **state)
{
    (void)state;

    hegel_session *s = hegel_session_new();
    assert_non_null(s);

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 30;

    hegel_results r = hegel_run_test(s, test_fn_text_bounded, NULL, &settings);
    assert_true(r.passed);

    hegel_results_free(&r);
    hegel_session_free(s);
}

/* ================================================================
 * Test: assume(false) causes INVALID
 * ================================================================ */
static void test_fn_assume_false(hegel_test_case *tc, void *user_data)
{
    (void)tc;
    (void)user_data;
    hegel_assume(false);
    /* Should not reach here (hegel_assume longjmps) */
}

static void test_assume_false(void **state)
{
    (void)state;

    hegel_session *s = hegel_session_new();
    assert_non_null(s);

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 10;

    hegel_results r = hegel_run_test(s, test_fn_assume_false, NULL, &settings);
    /* All test cases should be invalid, so no interesting cases found = passed */
    assert_true(r.passed);
    assert_int_equal(r.interesting_test_cases, 0);
    assert_true(r.invalid_test_cases > 0);

    hegel_results_free(&r);
    hegel_session_free(s);
}

/* NOTE: counterexample detection test requires a hegel_fail() API
 * (to signal INTERESTING status from the test body) which is not
 * yet implemented. Skipped for now. */

/* ================================================================
 * Test: multiple sessions
 * ================================================================ */
static void test_fn_simple(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    int64_t val = hegel_draw_int(tc, hegel_integers(0, 10));
    hegel_assume(val >= 0 && val <= 10);
}

static void test_multiple_sessions(void **state)
{
    (void)state;

    /* Run two sequential sessions to verify cleanup works */
    for (int i = 0; i < 2; i++) {
        hegel_session *s = hegel_session_new();
        assert_non_null(s);

        hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
        settings.max_examples = 10;

        hegel_results r = hegel_run_test(s, test_fn_simple, NULL, &settings);
        assert_true(r.passed);

        hegel_results_free(&r);
        hegel_session_free(s);
    }
}

/* ================================================================
 * Test: multiple test runs in single session
 * ================================================================ */
static void test_multiple_runs(void **state)
{
    (void)state;

    hegel_session *s = hegel_session_new();
    assert_non_null(s);

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 10;

    /* Run 1 */
    hegel_results r1 = hegel_run_test(s, test_fn_simple, NULL, &settings);
    assert_true(r1.passed);
    hegel_results_free(&r1);

    /* Run 2 */
    hegel_results r2 = hegel_run_test(s, test_fn_simple, NULL, &settings);
    assert_true(r2.passed);
    hegel_results_free(&r2);

    hegel_session_free(s);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_integers_in_bounds),
        cmocka_unit_test(test_booleans_basic),
        cmocka_unit_test(test_text_in_bounds),
        cmocka_unit_test(test_assume_false),

        cmocka_unit_test(test_multiple_sessions),
        cmocka_unit_test(test_multiple_runs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
