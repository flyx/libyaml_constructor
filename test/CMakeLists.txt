enable_testing()

function(test_case directory name)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.h
      ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.c
      COMMAND yaml_constructor_generator ${CMAKE_CURRENT_SOURCE_DIR}/${directory}/${directory}.h - -I "${PROJECT_SOURCE_DIR}/runtime/include"
      DEPENDS yaml_constructor_generator ${directory}/${directory}.h
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_executable(${directory} ${directory}/${directory}.h ${directory}/${directory}.c
      ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.h
      ${CMAKE_CURRENT_BINARY_DIR}/${directory}_loading.c common/test_common.h)
  target_include_directories(${directory}
      PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/runtime/include
      ${directory} ${LibYaml_INCLUDE_DIRS})
  target_link_libraries(${directory} ${LibYaml_LIBRARIES} yaml_constructor)
  set_property(TARGET ${directory} PROPERTY C_STANDARD 99)
  if(MSVC)
    add_custom_command(TARGET ${directory} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LibYaml_DLL}
        $<TARGET_FILE_DIR:${directory}>)
  endif()
  add_test(${name} ${directory})
endfunction(test_case)

test_case(simple "Simple")
test_case(variants "Tagged Unions")
test_case(pointers "Pointer Types")
test_case(optional "Optional Fields")
test_case(custom-constructor "Custom Constructor")