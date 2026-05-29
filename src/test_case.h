#ifndef HEGEL_TEST_CASE_H
#define HEGEL_TEST_CASE_H

#include "hegel/hegel.h"
#include "hegel/protocol.h"

#include <setjmp.h>
#include <stdbool.h>

/*
 * Reason codes for non-local exits from the test function body.
 */
typedef enum {
    HEGEL_JMP_ASSUME = 1,    /* hegel_assume(false) */
    HEGEL_JMP_STOP_TEST = 2, /* Server sent StopTest error */
    HEGEL_JMP_FAIL = 3       /* hegel_fail() called */
} hegel_jmp_reason;

/*
 * Test case context. Passed to the user's test function and used internally
 * to track the stream, status, and longjmp targets.
 */
struct hegel_test_case {
    hegel_stream *stream;
    bool is_final;
    bool aborted;
    hegel_status status;

    /* longjmp target for hegel_assume(false) and StopTest */
    jmp_buf escape_jmp;
    hegel_jmp_reason jmp_reason;

    /* Generator currently being drawn, if any.  A StopTest/assume/fail
       longjmp can unwind out of hegel_draw_raw before it frees the
       generator it was handed; the run_test setjmp handler frees this so
       the consume-on-draw contract does not leak on the longjmp path. */
    void *inflight_gen;

    /* Error message captured when status is INTERESTING */
    char *error_message;
};

/*
 * Get/set the thread-local current test case.
 */
hegel_test_case *hegel_current_test_case(void);
void hegel_set_current_test_case(hegel_test_case *tc);

#endif /* HEGEL_TEST_CASE_H */
