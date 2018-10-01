#ifndef CUSTOM_CONSTRUCTOR_H
#define CUSTOM_CONSTRUCTOR_H

#include <stdlib.h>
#include <stdbool.h>
#include <yaml_loader.h>

//!custom
struct diceroll_t {
  size_t dice_count;
  size_t face_count;
};

bool yaml_construct_struct_diceroll_t(struct diceroll_t *const value,
                                      yaml_loader_t *const loader,
                                      yaml_event_t *cur);
void yaml_delete_struct_diceroll_t(struct diceroll_t *const value);

//!list
struct dicerolls_t {
  struct diceroll_t *data;
  size_t count, capacity;
};

struct root {
  struct diceroll_t primary_roll;
  struct dicerolls_t additional_rolls;
};

#endif

