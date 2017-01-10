cmake_minimum_required (VERSION 2.6)
project (rete_cpp)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()



include_directories ("${PROJECT_SOURCE_DIR}/header")

add_library(rete_cpp ${SRC})
target_link_libraries(rete_cpp ${CONAN_LIBS})
