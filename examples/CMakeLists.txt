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
