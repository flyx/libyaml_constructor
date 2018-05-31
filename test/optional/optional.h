#ifndef OPTIONAL_H
#define OPTIONAL_H

#include <stdint.h>

struct some_object {
  size_t value;
};

struct root {
  //!optional
  struct some_object* optional_object;
  //!optional
  int* i;
  char* string;
};

#endif
