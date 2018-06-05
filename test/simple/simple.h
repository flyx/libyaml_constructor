#include <stddef.h>
#include <stdbool.h>

#ifndef _SIMPLE_H
#define _SIMPLE_H

enum gender_t {
  //!repr male
  MALE = 0,
  //!repr female
  FEMALE = 1,
  //!repr other
  OTHER = 2
};

struct person {
  //!string
  char* name;
  int age;
  enum gender_t gender;
  float height;
};

//!list
typedef struct person_list_s {
  struct person* data;
  size_t count;
  size_t capacity;
} person_list;

struct root {
  char symbol;
  bool toggle;
  person_list people;
};

#endif