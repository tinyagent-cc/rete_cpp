add_library(catch_main OBJECT test_main.cpp)
target_include_directories(catch_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(TEST_SOURCES
    test_types.cpp
    test_alpha_network.cpp
    test_beta_network.cpp
    test_rete.cpp
    test_inference_engine.cpp
    test_rule_loader.cpp
)

# The parser tests are per-profile because the headers they exercise are not
# compiled at all when their format is off. Guarding here rather than wrapping
# each file in #if keeps a disabled profile from reporting a passing test that
# contains nothing.
if(RETE_ENABLE_JSON)
    list(APPEND TEST_SOURCES test_rules_json.cpp)
endif()

if(RETE_ENABLE_YAML)
    list(APPEND TEST_SOURCES test_rules_yaml.cpp)
endif()

# The equivalence claim only means something when both readers are present.
if(RETE_ENABLE_JSON AND RETE_ENABLE_YAML)
    list(APPEND TEST_SOURCES test_rules_equivalence.cpp)
endif()

foreach(src ${TEST_SOURCES})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src} $<TARGET_OBJECTS:catch_main>)
    target_link_libraries(${test_name} PRIVATE rete)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ---------------------------------------------------------------------------
# Embedded profile contract
#
# The MCU targets are the reason the rules layer has this shape, and they are
# the one configuration nobody runs by accident. This target pins it down at
# build time: no exceptions, no YAML, no iostream of its own, no POSIX. It is
# built in every profile, so a host-only dependency creeping into rules/ breaks
# the build that same day rather than the day someone flashes a Pico.
#
# It deliberately does not link the `rete` interface target: that target carries
# the ambient profile's RETE_* definitions, which would collide with the
# embedded ones set here. Include path only, definitions of its own.
# ---------------------------------------------------------------------------
add_executable(test_rules_embedded test_rules_embedded.cpp)
target_include_directories(test_rules_embedded PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(test_rules_embedded PRIVATE cxx_std_17)
target_compile_definitions(test_rules_embedded PRIVATE
    RETE_EMBEDDED=1
    RETE_ENABLE_JSON=1
    RETE_ENABLE_YAML=0
)
if(MSVC)
    # MSVC's spelling of -fno-exceptions.
    target_compile_options(test_rules_embedded PRIVATE /EHs- /EHc-)
else()
    target_compile_options(test_rules_embedded PRIVATE -fno-exceptions)
endif()
add_test(NAME test_rules_embedded COMMAND test_rules_embedded)
