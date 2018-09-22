add_executable(yaml_constructor_generator yaml_constructor_generator.c cmdline_config.h cmdline_config.c)
target_include_directories(yaml_constructor_generator PRIVATE ${LibClang_INCLUDE_DIRS})
target_link_libraries(yaml_constructor_generator ${LibClang_LIBRARIES})
set_property(TARGET yaml_constructor_generator PROPERTY C_STANDARD 99)

if(MSVC)
  add_custom_command(TARGET yaml_constructor_generator POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LibClang_DLL}
          $<TARGET_FILE_DIR:yaml_constructor_generator>)
endif(MSVC)
