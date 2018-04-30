add_executable(libyaml_mapper libyaml_mapper.c)
target_include_directories(libyaml_mapper PRIVATE ${LibClang_INCLUDE_DIRS})
target_link_libraries(libyaml_mapper ${LibClang_LIBRARIES})
set_property(TARGET libyaml_mapper PROPERTY C_STANDARD 99)

if(MSVC)
  add_custom_command(TARGET libyaml_mapper POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LibClang_DLL}
          $<TARGET_FILE_DIR:libyaml_mapper>)
endif(MSVC)
