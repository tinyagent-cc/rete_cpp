set(EXAMPLE_SOURCES
    animal_classification.cpp
    blocks_world.cpp
    medical_diagnosis.cpp
)

foreach(src ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${src} NAME_WE)
    add_executable(${example_name} ${src})
    target_link_libraries(${example_name} PRIVATE rete)
endforeach()

# The robotics example reads its behaviour from a file, so it needs a reader
# compiled in. On a core-only build there is nothing for it to load, and a
# target that can only fail is worse than a target that is not there.
#
# RETE_EMBEDDED is excluded for a different reason: it sets RETE_NO_IOSTREAM,
# which removes load_from_file() along with <fstream>. An MCU target holds its
# rules in a flash string and calls load_from_string(), and it does not print a
# scenario trace to a console it does not have.
if(RETE_ENABLE_JSON AND NOT RETE_EMBEDDED)
    add_executable(robotics_behaviour robotics/main.cpp)
    target_link_libraries(robotics_behaviour PRIVATE rete)

    # Default rules path. Baking the source directory in lets the binary run
    # from any working directory; argv[1] overrides it.
    target_compile_definitions(robotics_behaviour PRIVATE
        RETE_ROBOTICS_RULES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/robotics")

    # A second copy next to the binary, for editing thresholds without dirtying
    # the source tree.
    add_custom_command(TARGET robotics_behaviour POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/robotics/rules.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/robotics/rules.yaml"
                "$<TARGET_FILE_DIR:robotics_behaviour>"
        COMMENT "Copying robotics rule files next to the binary")
endif()
