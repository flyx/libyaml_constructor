find_package(LibYaml REQUIRED)

enable_testing()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.h
                          ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.c
        COMMAND libyaml_mapper ${CMAKE_CURRENT_SOURCE_DIR}/simple/simple.h
        DEPENDS libyaml_mapper simple/simple.h
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_executable(simple simple/simple.h simple/simple.c
        ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.h
        ${CMAKE_CURRENT_BINARY_DIR}/simple_loading.c common/test_common.h)
target_include_directories(simple
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/include simple)
target_link_libraries(simple ${LibYaml_LIBRARIES})
set_property(TARGET simple PROPERTY C_STANDARD 99)
add_test("Simple test" simple)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/variants_loading.h
                          ${CMAKE_CURRENT_BINARY_DIR}/variants_loading.c
        COMMAND libyaml_mapper ${CMAKE_CURRENT_SOURCE_DIR}/variants/variants.h
        DEPENDS libyaml_mapper variants/variants.h
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_executable(variants variants/variants.h variants/variants.c
        ${CMAKE_CURRENT_BINARY_DIR}/variants_loading.h
        ${CMAKE_CURRENT_BINARY_DIR}/variants_loading.c common/test_common.h)
target_include_directories(variants
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/include variants)
target_link_libraries(variants ${LibYaml_LIBRARIES})
set_property(TARGET variants PROPERTY C_STANDARD 99)