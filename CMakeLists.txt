cmake_minimum_required(VERSION 3.14)
project(hegel VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(CBOR REQUIRED IMPORTED_TARGET libcbor)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# --------------------------------------------------------------------------
# Source files
# --------------------------------------------------------------------------

file(GLOB_RECURSE HEGEL_SOURCES CONFIGURE_DEPENDS "src/*.c")

# --------------------------------------------------------------------------
# Static library
# --------------------------------------------------------------------------

add_library(hegel_static STATIC ${HEGEL_SOURCES})
target_include_directories(hegel_static
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(hegel_static
    PUBLIC  PkgConfig::CBOR
            ZLIB::ZLIB
            Threads::Threads
)
set_target_properties(hegel_static PROPERTIES
    OUTPUT_NAME hegel
    POSITION_INDEPENDENT_CODE ON
)

# --------------------------------------------------------------------------
# Shared library
# --------------------------------------------------------------------------

add_library(hegel SHARED ${HEGEL_SOURCES})
target_include_directories(hegel
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(hegel
    PUBLIC  PkgConfig::CBOR
            ZLIB::ZLIB
            Threads::Threads
)
set_target_properties(hegel PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# --------------------------------------------------------------------------
# Tests (cmocka)
# --------------------------------------------------------------------------

option(HEGEL_BUILD_TESTS "Build unit tests" ON)

if(HEGEL_BUILD_TESTS)
    enable_testing()
    pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "tests/test_*.c")
    foreach(test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name}
            PRIVATE hegel_static
                    PkgConfig::CMOCKA
        )
        target_include_directories(${test_name}
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# --------------------------------------------------------------------------
# Conformance test binaries
# --------------------------------------------------------------------------

option(HEGEL_BUILD_CONFORMANCE "Build conformance test binaries" ON)

if(HEGEL_BUILD_CONFORMANCE)
    set(CONFORMANCE_HELPERS tests/conformance/json_helpers.c)
    file(GLOB CONFORMANCE_SOURCES CONFIGURE_DEPENDS "tests/conformance/test_*.c")
    foreach(conf_src ${CONFORMANCE_SOURCES})
        get_filename_component(conf_name ${conf_src} NAME_WE)
        add_executable(${conf_name} ${conf_src} ${CONFORMANCE_HELPERS})
        target_link_libraries(${conf_name}
            PRIVATE hegel_static
        )
        target_include_directories(${conf_name}
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance
        )
        set_target_properties(${conf_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/conformance
        )
    endforeach()
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS hegel hegel_static
    EXPORT  hegelTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/hegel
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT hegelTargets
    FILE        hegelTargets.cmake
    NAMESPACE   hegel::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hegel
)
