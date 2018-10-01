#include "custom-constructor.h"
#include <custom-constructor_loading.h>

#include <../common/test_common.h>
#include <yaml.h>
#include <yaml_loader.h>

static const char* input =
    "primary_roll: 3d20\n"
    "additional_rolls:\n"
    "- 1d20\n"
    "- 1d6\n";

bool yaml_construct_struct_diceroll_t(struct diceroll_t *const value,
                                      yaml_loader_t *const loader,
                                      yaml_event_t *cur) {
  if (cur->type != YAML_SCALAR_EVENT) {
    loader->error_info.type = YAML_LOADER_ERROR_STRUCTURAL;
    loader->error_info.event = *cur;
    loader->error_info.expected_event_type = YAML_SCALAR_EVENT;
    return false;
  }
  const char *content = (const char*)cur->data.scalar.value;

  char *endptr;
  value->dice_count = strtoul(content, &endptr, 10);

  if (*endptr != 'd') {
    loader->error_info.type = YAML_LOADER_ERROR_VALUE;
    loader->error_info.event = *cur;
    return false;
  }
  value->face_count = strtoul(endptr + 1, &endptr, 10);
  if (*endptr != '\0') {
    loader->error_info.type = YAML_LOADER_ERROR_VALUE;
    loader->error_info.event = *cur;
    return false;
  }
  return true;
}

void yaml_delete_struct_diceroll_t(struct diceroll_t *const value) {}

int main(int argc, char* argv[]) {
  yaml_loader_t loader;
  yaml_loader_init_string(&loader, (const unsigned char*)input, strlen(input));
  struct root data;
  bool ret = yaml_load_struct_root(&data, &loader);
  yaml_loader_delete(&loader);

  if (!ret) {
    fprintf(stderr, "error while loading YAML doc.");
    return 1;
  } else {
    bool success = true;
    ASSERT_EQUALS_SIZE((size_t)3, data.primary_roll.dice_count, success);
    ASSERT_EQUALS_SIZE((size_t)20, data.primary_roll.face_count, success);

    ASSERT_EQUALS_SIZE((size_t)2, data.additional_rolls.count, success);

    ASSERT_EQUALS_SIZE((size_t)1, data.additional_rolls.data[0].dice_count,
                       success);
    ASSERT_EQUALS_SIZE((size_t)20, data.additional_rolls.data[0].face_count,
                       success);

    ASSERT_EQUALS_SIZE((size_t)1, data.additional_rolls.data[1].dice_count,
                       success);
    ASSERT_EQUALS_SIZE((size_t)6, data.additional_rolls.data[1].face_count,
                       success);

    return success ? 0 : 1;
  }
}