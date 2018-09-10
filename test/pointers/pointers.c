#include "pointers.h"
#include <pointers_loading.h>
#include <stdbool.h>

#include <yaml.h>
#include <../common/test_common.h>

static const char* input =
    "first: {number: 47}\n"
    "second: {string: spam egg sausage and spam}\n";

int main(int argc, char* argv[]) {
  yaml_parser_t parser;
  yaml_parser_initialize(&parser);
  yaml_parser_set_input_string(&parser, (const unsigned char*)input, strlen(input));
  struct root data;
  char* ret = load_one_struct__root(&data, &parser);
  yaml_parser_delete(&parser);

  if (ret) {
    fprintf(stderr, "error while loading YAML:\n%s\n", ret);
    return 1;
  } else {
    bool success = true;
    ASSERT_EQUALS_INT(47, data.first->number, success);
    ASSERT_EQUALS_STRING("spam egg sausage and spam", data.second->string, success);

    return success ? 0 : 1;
  }
}