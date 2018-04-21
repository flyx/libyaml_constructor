#include "simple.h"
#include <simple_loading.h>

#include <yaml.h>

static const char* input =
    "symbol: W\n"
    "people:\n"
    "  - name: Peter Pan\n"
    "    age: 12\n"
    "    gender: female\n"
    "  - name: Karl Koch\n"
    "    age: 27\n"
    "    gender: male\n"
    "  - name: Scrooge McDuck\n"
    "    age: 75\n"
    "    gender: other\n";

int main(int argc, char* argv[]) {
  yaml_parser_t parser;
  yaml_parser_initialize(&parser);
  yaml_parser_set_input_string(&parser, (const unsigned char*)input, strlen(input));
  struct root data;
  char* ret = load_one(&data, &parser);

  static const char* gender_repr[] = {"male", "female", "other"};
  if (ret) {
    fprintf(stderr, "error while loading YAML:\n%s\n", ret);
    return 1;
  } else {
    printf("symbol = %c\nPersons:\n", data.symbol);
    for (size_t i = 0; i < data.people.count; ++i) {
      struct person* item = &data.people.data[i];
      printf("  %s, %s, age %i\n", item->name, gender_repr[item->gender],
             item->age);
    }
    yaml_parser_delete(&parser);
    return 0;
  }
}