find_package(LibYaml REQUIRED)

enable_testing()

function(test_case directory name)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.h
            ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.c
            COMMAND libyaml_mapper ${CMAKE_CURRENT_SOURCE_DIR}/${directory}/${directory}.h
            DEPENDS libyaml_mapper ${directory}/${directory}.h
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_executable(${directory} ${directory}/${directory}.h ${directory}/${directory}.c
            ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.h
            ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.c common/test_common.h)
    target_include_directories(${directory}
            PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/include ${directory})
    target_link_libraries(${directory} ${LibYaml_LIBRARIES})
    set_property(TARGET ${directory} PROPERTY C_STANDARD 99)
    add_test(${name} ${directory})
endfunction(test_case)

test_case(simple "Simple")
test_case(variants "Tagged Unions")
test_case(pointers "Pointer Types")
