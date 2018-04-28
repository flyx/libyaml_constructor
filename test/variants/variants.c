#include "variants.h"
#include <variants_loading.h>
#include <stdbool.h>

#include <yaml.h>
#include <../common/test_common.h>

static const char* input =
    "- !string lorem ipsum\n"
    "- !int 42\n"
    "- !none\n"
    "- !char X\n";

int main(int argc, char* argv[]) {
  yaml_parser_t parser;
  yaml_parser_initialize(&parser);
  yaml_parser_set_input_string(&parser, (const unsigned char *) input, strlen(input));
  struct root data;
  char *ret = load_one(&data, &parser);
  yaml_parser_delete(&parser);

  static const char* type_repr[] =
      {"CHAR_VALUE", "INT_VALUE", "STRING_VALUE", "NO_VALUE"};
  if (ret) {
    fprintf(stderr, "error while loading YAML:\n%s\n", ret);
    return 1;
  } else {
    bool success = true;
    ASSERT_EQUALS_SIZE((size_t)4, data.count, success);
    ASSERT_EQUALS_ENUM(STRING_VALUE, data.data[0].type, success, type_repr);
    ASSERT_EQUALS_STRING("lorem ipsum", data.data[0].s, success);
    ASSERT_EQUALS_ENUM(INT_VALUE, data.data[1].type, success, type_repr);
    ASSERT_EQUALS_INT(42, data.data[1].i, success);
    ASSERT_EQUALS_ENUM(NO_VALUE, data.data[2].type, success, type_repr);
    ASSERT_EQUALS_ENUM(CHAR_VALUE, data.data[3].type, success, type_repr);
    ASSERT_EQUALS_CHAR('X', data.data[3].c, success);
    return success ? 0 : 1;
  }
}