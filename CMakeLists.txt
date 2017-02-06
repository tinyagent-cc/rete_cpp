cmake_minimum_required (VERSION 2.6)
project (sbre_cpp)

add_compile_options(-std=c++11)


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)


if(WIN32)
    set(BOOST_ROOT "C:/dev/boost_1_63_0" )
    #set(Boost_INCLUDE_DIR C:/dev/boost_1_63_0/boost )
    set(BOOST_LIBRARYDIR "C:/dev/boost_1_63_0/stage/lib" )
endif(WIN32)
#  proto, mpl, fusion for expressions 
find_package( Boost COMPONENTS program_options proto mpl fusion log graph serialization  REQUIRED )

# Find the QtWidgets library
#find_package(Qt5Widgets)

#include_directories(${Boost_INCLUDE_DIR} ${MPI_INCLUDE_PATH} include)
include_directories(${Boost_INCLUDE_DIR} header)

add_library(${PROJECT_NAME} INTERFACE)
#target_sources(${PROJECT_NAME} INTERFACE lib2.h)
target_link_libraries(${PROJECT_NAME} INTERFACE ${Boost_LIBRARIES} )
