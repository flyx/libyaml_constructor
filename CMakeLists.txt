cmake_minimum_required(VERSION 3.10)

project(libyaml_mapper VERSION 0.1.0 DESCRIPTION "generator for code that loads YAML into your type hierarchy" LANGUAGES C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/Modules/")

find_package(LibClang REQUIRED)
find_package(LibYaml REQUIRED)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS /wd4028)
endif()

add_subdirectory(src)
add_subdirectory(test)

