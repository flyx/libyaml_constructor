#include <stddef.h>

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
};

//!list
typedef struct person_list_s {
  struct person* data;
  size_t count;
  size_t capacity;
} person_list;

struct root {
  char symbol;
  person_list people;
};

#endif