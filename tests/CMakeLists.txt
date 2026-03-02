add_library(catch_main OBJECT test_main.cpp)
target_include_directories(catch_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(TEST_SOURCES
    test_types.cpp
    test_alpha_network.cpp
    test_beta_network.cpp
    test_rete.cpp
    test_inference_engine.cpp
)

foreach(src ${TEST_SOURCES})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src} $<TARGET_OBJECTS:catch_main>)
    target_link_libraries(${test_name} PRIVATE rete)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
