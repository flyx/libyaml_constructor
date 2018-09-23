#include "simple.h"
#include <simple_loading.h>
#include <stdbool.h>

#include <yaml_loader.h>
#include <../common/test_common.h>

static const char* input =
    "symbol: W\n"
    "people:\n"
    "  - name: Ada Lovelace\n"
    "    age: 36\n"
    "    gender: female\n"
    "    height: 1.65\n"
    "  - name: Karl Koch\n"
    "    age: 27\n"
    "    gender: male\n"
    "    height: 1.73\n"
    "  - name: Scrooge McDuck\n"
    "    age: 75\n"
    "    gender: other\n"
    "    height: 0.91\n"
    "toggle: true";

int main(int argc, char* argv[]) {
  yaml_loader_t loader;
  yaml_loader_init_string(&loader, (const unsigned char*)input, strlen(input));

  struct root data;
  bool ret = load_one_struct__root(&data, &loader);

  static const char* gender_repr[] = {"male", "female", "other"};
  if (!ret) {
    fprintf(stderr, "error while loading YAML.\n");
    yaml_loader_delete(&loader);

    return 1;
  } else {
    bool success = true;
    ASSERT_EQUALS_CHAR('W', data.symbol, success);
    ASSERT_EQUALS_BOOL(true, data.toggle, success);

    ASSERT_EQUALS_STRING("Ada Lovelace", data.people.data[0].name, success);
    ASSERT_EQUALS_ENUM(FEMALE, data.people.data[0].gender, success, gender_repr);
    ASSERT_EQUALS_INT(36, data.people.data[0].age, success);
    ASSERT_EQUALS_FLOAT(1.65, data.people.data[0].height, success);

    ASSERT_EQUALS_STRING("Karl Koch", data.people.data[1].name, success);
    ASSERT_EQUALS_ENUM(MALE, data.people.data[1].gender, success, gender_repr);
    ASSERT_EQUALS_INT(27, data.people.data[1].age, success);
    ASSERT_EQUALS_FLOAT(1.73, data.people.data[1].height, success);

    ASSERT_EQUALS_STRING("Scrooge McDuck", data.people.data[2].name, success);
    ASSERT_EQUALS_ENUM(OTHER, data.people.data[2].gender, success, gender_repr);
    ASSERT_EQUALS_INT(75, data.people.data[2].age, success);
    ASSERT_EQUALS_FLOAT(0.91, data.people.data[2].height, success);

    yaml_loader_delete(&loader);

    return success ? 0 : 1;
  }
}