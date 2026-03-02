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
