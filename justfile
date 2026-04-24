# `just` prints bash comments in stdout by default. this suppresses that
set ignore-comments := true

default: check

check: lint test

build:
    cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build build

test:
    cmake -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    cd build && ctest --output-on-failure

lint:
    @echo "Checking formatting..."
    find src include tests -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror

format:
    find src include tests -name '*.c' -o -name '*.h' | xargs clang-format -i

clean:
    rm -rf build

conformance: build
    @echo "Running conformance tests..."
    uv run --with 'hegel-core==0.4.1' --with pytest --with pytest-subtests --with hypothesis \
        pytest tests/conformance/test_conformance.py -v --tb=short

coverage:
    cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="--coverage" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
    cmake --build build
    cd build && ctest --output-on-failure
    @echo "Generating coverage report..."
    gcovr --root . --filter src/ --branch --fail-under-branch 100
