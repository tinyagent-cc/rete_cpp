cmake_minimum_required(VERSION 3.14)
project(rete_cpp VERSION 1.0.0 LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(rete INTERFACE)

# Without this, a downstream consumer that does not set CMAKE_CXX_STANDARD
# itself inherits the compiler default (C++14 on AppleClang) and fails on
# std::variant in types.hpp. In-repo builds hid it because the top-level
# CMAKE_CXX_STANDARD above covers this directory.
target_compile_features(rete INTERFACE cxx_std_17)

target_include_directories(rete INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

find_package(Boost CONFIG QUIET)
if(TARGET Boost::headers)
    target_link_libraries(rete INTERFACE Boost::headers)
    target_compile_definitions(rete INTERFACE RETE_HAS_BOOST=1)
    message(STATUS "Boost found -- enabling optional BGL DOT export")
else()
    message(STATUS "Boost not found -- BGL DOT export disabled (core functionality unaffected)")
endif()

option(RETE_ENABLE_JSON "Build the JSON rule loader" ON)
option(RETE_ENABLE_YAML "Build the YAML rule loader (needs yaml-cpp; host only)" OFF)
option(RETE_EMBEDDED "Constrained-MCU profile: JSON only, no iostream, no exceptions" OFF)

# config.hpp already #errors on this combination, but a compile error inside a
# header is a poor way to learn that two cache variables disagree.
if(RETE_EMBEDDED AND RETE_ENABLE_YAML)
    message(FATAL_ERROR
        "RETE_EMBEDDED=ON cannot be combined with RETE_ENABLE_YAML=ON. "
        "Embedded targets load rules as JSON; see docs/rules.md.")
endif()

set(_rete_profile "core")

# The options and the header defaults are two sources of truth. Pass each one
# through as an explicit 0 or 1 rather than trusting them to agree.
if(RETE_ENABLE_JSON)
    target_compile_definitions(rete INTERFACE RETE_ENABLE_JSON=1)
    string(APPEND _rete_profile "+json")
else()
    target_compile_definitions(rete INTERFACE RETE_ENABLE_JSON=0)
endif()

if(RETE_ENABLE_YAML)
    find_package(yaml-cpp CONFIG QUIET)
    if(TARGET yaml-cpp::yaml-cpp)
        target_link_libraries(rete INTERFACE yaml-cpp::yaml-cpp)
    elseif(TARGET yaml-cpp)
        target_link_libraries(rete INTERFACE yaml-cpp)
    else()
        # Falling back to a JSON-only build would be the worst outcome here: a
        # robot that boots, silently loads nothing, and runs with no behaviour.
        message(FATAL_ERROR
            "RETE_ENABLE_YAML=ON but yaml-cpp was not found. Install it "
            "(brew install yaml-cpp, apt install libyaml-cpp-dev), point "
            "CMAKE_PREFIX_PATH at it, or configure with -DRETE_ENABLE_YAML=OFF.")
    endif()
    target_compile_definitions(rete INTERFACE RETE_ENABLE_YAML=1)
    string(APPEND _rete_profile "+yaml(${yaml-cpp_VERSION})")
else()
    target_compile_definitions(rete INTERFACE RETE_ENABLE_YAML=0)
endif()

if(RETE_EMBEDDED)
    target_compile_definitions(rete INTERFACE RETE_EMBEDDED=1)
    string(APPEND _rete_profile "+embedded")
else()
    target_compile_definitions(rete INTERFACE RETE_EMBEDDED=0)
endif()

message(STATUS "rete profile: ${_rete_profile}")

option(RETE_BUILD_TESTS "Build tests" ON)
option(RETE_BUILD_EXAMPLES "Build examples" ON)
option(RETE_BUILD_BENCHMARKS "Build benchmarks" ON)

if(RETE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RETE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RETE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
