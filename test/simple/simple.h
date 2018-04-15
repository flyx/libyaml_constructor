#include <stddef.h>

#ifndef _SIMPLE_H
#define _SIMPLE_H

struct person {
  //!string
  char* name;

  int age;
};

//!list
struct person_list {
  struct person* data;
  size_t count;
  size_t capacity;
};

struct root {
  char symbol;
  struct person_list people;
};

#endif