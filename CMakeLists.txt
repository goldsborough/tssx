###########################################################
## PROJECT
###########################################################

cmake_minimum_required(VERSION 3.2)
project(tssx)

set(CMAKE_MACOSX_RPATH 1)

add_compile_options(-pthread)

###########################################################
## INCLUDES
###########################################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

###########################################################
## SUBDIRECTORIES
###########################################################

add_subdirectory(source)
add_subdirectory(try)
