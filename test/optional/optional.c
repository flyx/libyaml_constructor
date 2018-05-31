#include "optional.h"
#include <optional_loading.h>
#include <stdbool.h>

#include <yaml.h>
#include <../common/test_common.h>

static const char* input =
    "optional_object: {value: 255}\n"
    "string: hello\n"
    "---\n"
    "string: world\n"
    "i: 52\n";

int main(int argc, char* argv[]) {
  yaml_parser_t parser;
  yaml_parser_initialize(&parser);
  yaml_parser_set_input_string(&parser, (const unsigned char*)input, strlen(input));
  struct root data1, data2;
  char* ret1 = load_one(&data1, &parser);
  char* ret2 = load_one(&data2, &parser);
  yaml_parser_delete(&parser);

  if (ret1) {
    fprintf(stderr, "error while loading YAML:\n%s\n", ret1);
    return 1;
  } else if (ret2) {
    fprintf(stderr, "error while loading YAML:\n%s\n", ret2);
    return 1;
  } else {
    bool success = true;
    ASSERT_NOT_NULL(data1.optional_object, success);
    ASSERT_EQUALS_SIZE((size_t)255, data1.optional_object->value, success);
    ASSERT_NULL(data1.i, success);
    ASSERT_EQUALS_STRING("hello", data1.string, success);

    ASSERT_NULL(data2.optional_object, success);
    ASSERT_NOT_NULL(data2.i, success);
    ASSERT_EQUALS_INT(52, *data2.i, success);
    ASSERT_EQUALS_STRING("world", data2.string, success);

    return success ? 0 : 1;
  }
}