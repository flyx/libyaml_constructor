#ifndef _VARIANTS_H
#define _VARIANTS_H

#include <stdlib.h>

enum value_type {
  //!repr char
  CHAR_VALUE,
  //!repr int
  INT_VALUE,
  //!repr string
  STRING_VALUE,
  //!repr none
  NO_VALUE
};

//!tagged
struct field {
  enum value_type type;

  union {
    char c;
    int i;
    //!string
    char* s;
  };
};

//!list
struct root {
  struct field* data;
  size_t count;
  size_t capacity;
};

#endif
