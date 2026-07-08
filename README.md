> [!WARNING]
> **This repository is deprecated and read-only.**
>
> This was an unofficial C client for the old Hegel socket protocol, which
> Hegel no longer uses. The protocol it implements has been removed upstream
> and this code no longer works against current Hegel.
>
> There is now an official Hegel C library. Use it instead:
>
> - Source: https://github.com/hegeldev/hegel-rust/tree/main/hegel-c
> - Prebuilt `libhegel` releases: https://github.com/hegeldev/hegel-rust/releases
> - Website: https://hegel.dev
>
> The official library uses an in-process FFI approach (a native shared
> library with a C ABI), not the subprocess/socket protocol this repository
> was built around. No migration path from this code is provided.

# Hegel for C

* [Hegel website](https://hegel.dev)

`hegel-c` is a C99 property-based testing library. `hegel-c` is based on [Hypothesis](https://github.com/hypothesisworks/hypothesis), using the [Hegel protocol](https://hegel.dev/).

## Prerequisites

- C99 compiler (gcc or clang)
- [CMake](https://cmake.org/) 3.14+
- [libcbor](https://github.com/PJK/libcbor)
- [zlib](https://zlib.net/)
- [cmocka](https://cmocka.org/) (for tests)
- The `hegel` binary (from [hegel-core](https://github.com/hegeldev/hegel-core))

## Building

```bash
mkdir build && cd build
cmake ..
make
```

To run tests:

```bash
make test
```

To disable tests or conformance binaries:

```bash
cmake .. -DHEGEL_BUILD_TESTS=OFF -DHEGEL_BUILD_CONFORMANCE=OFF
```

## Quickstart

Here's a quick example of a property test that verifies generated integers stay within bounds:

```c
#include <hegel/hegel.h>
#include <hegel/generators.h>

static void my_test(hegel_test_case *tc, void *user_data)
{
    (void)user_data;
    int64_t val = hegel_draw_int(tc, hegel_integers(0, 100));
    assert(val >= 0 && val <= 100);
}

int main(void)
{
    hegel_session *s = hegel_session_new();

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 50;

    hegel_results r = hegel_run_test(s, my_test, NULL, &settings);
    /* r.passed is true if no test case was INTERESTING */

    hegel_results_free(&r);
    hegel_session_free(s);
    return r.passed ? 0 : 1;
}
```

Compile and link against `libhegel`, `libcbor`, and `zlib`:

```bash
gcc -std=c99 -o my_test my_test.c -lhegel -lcbor -lz
```

## Server Binary

The library spawns the `hegel` server as a subprocess. By default it searches your `PATH`. Set the `HEGEL_SERVER_COMMAND` environment variable to override:

```bash
export HEGEL_SERVER_COMMAND=/path/to/hegel
```

## API Overview

### Session Management

```c
hegel_session *s = hegel_session_new();   /* create a session (spawns server) */
hegel_session_free(s);                     /* tear down session */
```

### Running Tests

```c
hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
settings.max_examples = 100;

hegel_results r = hegel_run_test(s, test_fn, user_data, &settings);
/* inspect r.passed, r.valid_test_cases, r.interesting_test_cases, etc. */
hegel_results_free(&r);
```

### Generators

Primitive generators:

```c
hegel_integers(min, max)       /* int64_t in [min, max] */
hegel_floats(min, max)         /* double in [min, max] */
hegel_booleans()               /* bool */
hegel_text(min_size, max_size) /* UTF-8 string (caller frees) */
hegel_binary(min_size, max_size) /* byte buffer (caller frees) */
```

Collections:

```c
hegel_lists(element_gen, min_size, max_size)
hegel_lists_unique(element_gen, min_size, max_size)
hegel_tuples(gen_array, count)
hegel_dicts(key_gen, val_gen, min_size, max_size)
```

Constant values and sampling:

```c
hegel_just_int(42)
hegel_just_string("hello")
hegel_sampled_from_strings(values, count)
hegel_sampled_from_ints(values, count)
```

Format generators: `hegel_emails()`, `hegel_urls()`, `hegel_domains()`, `hegel_ip4_addresses()`, `hegel_ip6_addresses()`, `hegel_dates()`, `hegel_times()`, `hegel_datetimes()`.

### Combinators

```c
hegel_map(source, map_fn, ctx, free_fn)     /* transform values */
hegel_flat_map(source, flatmap_fn, ctx)      /* dependent generation */
hegel_filter(source, predicate_fn, ctx)      /* reject values */
hegel_one_of(gen_array, count)               /* choose from alternatives */
hegel_optional(element_gen)                  /* nullable variant */
```

### Draw Functions

Inside a test body, draw values from generators:

```c
int64_t  val  = hegel_draw_int(tc, gen);
double   fval = hegel_draw_float(tc, gen);
bool     bval = hegel_draw_bool(tc, gen);
char    *sval = hegel_draw_string(tc, gen);  /* caller frees */
size_t   len;
uint8_t *data = hegel_draw_bytes(tc, gen, &len); /* caller frees */
```

### Test Body Helpers

```c
hegel_assume(condition);           /* skip test case if false */
hegel_target(value, "label");      /* guide generation toward a goal */
hegel_note("diagnostic message");  /* attach a note to the test case */
```

## Specification

The full library specification is at [`core/docs/library-api.md`](https://github.com/hegeldev/hegel-core/blob/main/docs/library-api.md).

## License

See [LICENSE](LICENSE) for details.
