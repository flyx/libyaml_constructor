find_package(LibYaml REQUIRED)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.h
                          ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.c
        COMMAND libyaml_mapper ${CMAKE_CURRENT_SOURCE_DIR}/simple/simple.h
        DEPENDS simple/simple.h
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(simple simple/simple.h simple/simple.c
        ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.h
        ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.c)
target_include_directories(simple
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/include simple)
target_link_libraries(simple ${LibYaml_LIBRARIES})
set_property(TARGET simple PROPERTY C_STANDARD 99)