"""Conformance tests for Hegel C99 client library.

These tests validate that the C99 client correctly implements the Hegel protocol
by running compiled C binaries against the real hegel server and checking
that the generated values satisfy the expected constraints.
"""

from pathlib import Path

from hegel.conformance import (
    BinaryConformance,
    BooleanConformance,
    DictConformance,
    EmptyTestConformance,
    ErrorResponseConformance,
    FloatConformance,
    IntegerConformance,
    ListConformance,
    SampledFromConformance,
    StopTestOnCollectionMoreConformance,
    StopTestOnGenerateConformance,
    StopTestOnMarkCompleteConformance,
    StopTestOnNewCollectionConformance,
    TextConformance,
    run_conformance_tests,
)

# Path to the compiled conformance binaries.
# CMake builds them into build/tests/conformance/.
BUILD_DIR = Path(__file__).parent.parent.parent / "build" / "tests" / "conformance"

# C99 uses int64_t, so we use 64-bit integer bounds.
INT64_MIN = -(2**63)
INT64_MAX = 2**63 - 1


def test_conformance(subtests):
    run_conformance_tests(
        [
            BooleanConformance(BUILD_DIR / "test_booleans"),
            IntegerConformance(
                BUILD_DIR / "test_integers",
                min_value=INT64_MIN,
                max_value=INT64_MAX,
            ),
            FloatConformance(BUILD_DIR / "test_floats"),
            TextConformance(BUILD_DIR / "test_text", no_surrogates=True),
            BinaryConformance(BUILD_DIR / "test_binary"),
            ListConformance(
                BUILD_DIR / "test_lists",
                min_value=INT64_MIN,
                max_value=INT64_MAX,
            ),
            SampledFromConformance(BUILD_DIR / "test_sampled_from"),
            DictConformance(
                BUILD_DIR / "test_dicts",
                min_key=INT64_MIN,
                max_key=INT64_MAX,
                min_value=INT64_MIN,
                max_value=INT64_MAX,
            ),
        ],
        subtests,
        skip_tests=[
            # Not yet implemented in the C99 client
            StopTestOnGenerateConformance,
            StopTestOnMarkCompleteConformance,
            ErrorResponseConformance,
            EmptyTestConformance,
            StopTestOnCollectionMoreConformance,
            StopTestOnNewCollectionConformance,
        ],
    )
