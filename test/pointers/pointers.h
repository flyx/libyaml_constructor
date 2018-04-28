#ifndef _POINTERS_H
#define _POINTERS_H

struct first_object {
  int number;
};

typedef struct {
  //!string
  char* string;
} second_object;

struct root {
  struct first_object* first;
  second_object* second;
};

#endif
