#include "optional.h"
#include <optional_loading.h>
#include <stdbool.h>

#include <yaml_loader.h>
#include <../common/test_common.h>

static const char* input =
    "optional_object: {value: 255}\n"
    "string: hello\n"
    "---\n"
    "string: world\n"
    "i: 52\n"
    "optional_string: klaatu barada nikto";

int main(int argc, char* argv[]) {
  yaml_loader_t loader;
  yaml_loader_init_string(&loader, (const unsigned char*)input, strlen(input));
  struct root data1, data2;
  bool ret1 = yaml_load_struct_root(&data1, &loader);
  bool ret2 = yaml_load_struct_root(&data2, &loader);
  yaml_loader_delete(&loader);

  if (!ret1) {
    fprintf(stderr, "error while loading YAML doc #1.");
    return 1;
  } else if (!ret2) {
    fprintf(stderr, "error while loading YAML doc #2.");
    return 1;
  } else {
    bool success = true;
    ASSERT_NOT_NULL(data1.optional_object, success);
    ASSERT_EQUALS_SIZE((size_t)255, data1.optional_object->value, success);
    ASSERT_NULL(data1.i, success);
    ASSERT_EQUALS_STRING("hello", data1.string, success);
    ASSERT_NULL(data1.optional_string, success);

    ASSERT_NULL(data2.optional_object, success);
    ASSERT_NOT_NULL(data2.i, success);
    ASSERT_EQUALS_INT(52, *data2.i, success);
    ASSERT_EQUALS_STRING("world", data2.string, success);
    ASSERT_EQUALS_STRING("klaatu barada nikto", data2.optional_string, success);

    return success ? 0 : 1;
  }
}