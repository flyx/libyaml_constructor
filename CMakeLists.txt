cmake_minimum_required(VERSION 3.10)

project(libyaml_constructor VERSION 0.2.0 DESCRIPTION "constructor that loads YAML into native C types" LANGUAGES C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/Modules/")

find_package(LibClang REQUIRED)
find_package(LibYaml REQUIRED)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS /wd4028)
endif()

add_subdirectory(generator)
add_subdirectory(runtime)
add_subdirectory(test)

