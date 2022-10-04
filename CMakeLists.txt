cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXTRA_COMPONENT_DIRS components vendor)
set(PROJECT_VER "0.0.1") 

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bow)
