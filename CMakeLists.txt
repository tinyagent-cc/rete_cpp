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

#    if(MSVC) # Check if we are using the Visual Studio compiler
#        SET(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} /SUBSYSTEM:WINDOWS") # Tell the project how to behave in this environment
#    elseif(CMAKE_COMPILER_IS_GNUCXX)
#        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mwindows") # Tell the project how to behave in this environment
#    else()
#        message(SEND_ERROR "You are using an unsupported Windows compiler! (Not MSVC or GCC)")
#    endif()
endif(WIN32)
#  proto mpl fusion for expressions ( but they are header only so just added to include no link is needed)
#find_package( Boost COMPONENTS program_options  log graph serialization  REQUIRED )
find_package( Boost  REQUIRED )

# Find the QtWidgets library
#find_package(Qt5Widgets)

#include_directories(${Boost_INCLUDE_DIR} ${MPI_INCLUDE_PATH} include)
include_directories(${Boost_INCLUDE_DIR} include/core)

add_library(${PROJECT_NAME} INTERFACE)
#target_sources(${PROJECT_NAME} INTERFACE lib2.h)
target_link_libraries(${PROJECT_NAME} INTERFACE ${Boost_LIBRARIES} )
