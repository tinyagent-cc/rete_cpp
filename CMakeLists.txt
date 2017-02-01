cmake_minimum_required (VERSION 2.6)
project (sbre_cpp)

add_compile_options(-std=c++11)

find_package( Boost COMPONENTS  program_options log graph serialization  REQUIRED )

#include_directories(${Boost_INCLUDE_DIR} ${MPI_INCLUDE_PATH} include)
include_directories(${Boost_INCLUDE_DIR} header)

add_library(${PROJECT_NAME} INTERFACE)
#target_sources(${PROJECT_NAME} INTERFACE lib2.h)
target_link_libraries(${PROJECT_NAME} INTERFACE ${Boost_LIBRARIES} )
