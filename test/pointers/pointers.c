#include "pointers.h"
#include <pointers_loading.h>
#include <stdbool.h>

#include <yaml_loader.h>
#include <../common/test_common.h>

static const char* input =
    "first: {number: 47}\n"
    "second: {string: spam egg sausage and spam}\n";

int main(int argc, char* argv[]) {
  yaml_loader_t loader;
  yaml_loader_init_string(&loader, (const unsigned char*)input, strlen(input));
  struct root data;
  bool ret = load_one_struct__root(&data, &loader);
  yaml_loader_delete(&loader);

  if (!ret) {
    fprintf(stderr, "error while loading YAML.");
    return 1;
  } else {
    bool success = true;
    ASSERT_EQUALS_INT(47, data.first->number, success);
    ASSERT_EQUALS_STRING("spam egg sausage and spam", data.second->string, success);

    return success ? 0 : 1;
  }
}