#ifndef OPTIONAL_H
#define OPTIONAL_H

#include <stdint.h>
#include <stddef.h>

struct some_object {
  size_t value;
};

struct root {
  //!optional
  struct some_object* optional_object;
  //!optional
  int* i;
  //!string
  char* string;
  //!optional_string
  char* optional_string;
};

#endif
