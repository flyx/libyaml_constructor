add_library(yaml_constructor STATIC
        src/yaml_constructor.c
        src/yaml_loader.c
        include/yaml_constructor.h
        include/yaml_loader.h)
target_include_directories(yaml_constructor PRIVATE include
        ${LibYaml_INCLUDE_DIRS})
target_link_libraries(yaml_constructor ${LibYaml_LIBRARIES})
set_property(TARGET yaml_constructor PROPERTY C_STANDARD 99)