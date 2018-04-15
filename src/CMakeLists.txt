add_executable(generator generator.c)
target_include_directories(generator PRIVATE ${LibClang_INCLUDE_DIRS})
target_link_libraries(generator ${LibClang_LIBRARIES})
