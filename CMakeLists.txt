cmake_minimum_required (VERSION 2.6)
project (sbre_cpp)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()



include_directories ("${PROJECT_SOURCE_DIR}/header")

add_library(sbre_cpp ${SRC})
target_link_libraries(sbre_cpp ${CONAN_LIBS})
