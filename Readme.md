# libyaml_mapper

**libyaml_mapper** is a tool that takes a C header with struct
definitions and autogenerates C code that loads YAML into the given
struct hierarchy. It uses the excellent [libclang][1] for parsing the
given header.

It consists of two parts:

 * The **libyaml_mapper** executable generates the code.
 * The `loader_common.h` header is included by the generated code.
   You need to provide access to it via `-I` when compiling the
   generated code.

## Status

I develop this project as part of a larger one. That means that it
probably will not get complete support for every feature the C type
system has in the forseeable future. I only add features when needed.

Since this project is in development, do not expect it to work smoothly
and be wary about its generated code, especially concerning memory leaks
when the YAML fails to load.

List of stuff that currently should work:

 * `int`
 * `char`
 * `struct` at top-level, with fields containing anything from this list
 * `enum` at top-level
 * `char*` as string
 * pointers to anything on this list apart from pointers
   (**note:** allocated things are currently not properly deallocated!)
 * dynamic lists (see below)
 * [tagged unions][2] (see below)
 * having reference to line and column in error messages

List of stuff that currently does not work:

 * serializing back to YAML
 * anonymous structs inside structs
 * any basic type other than `int` and `char`
 * reading the documentation (there is none apart from this Readme)

## Usage

    libyaml_mapper [options] file
      options:
        -o directory       writes output files to $directory (default: .)
        -r name            expects the root type to be named $name.
                           default: "root"
        -n name            names output files $name.h and $name.c .
                           default: ${file without ext}_loading.{h,c}.

In your code, you need to *annotate* certain structures so that
libyaml_mapper knows your intention. You annotate a type or field by
adding a comment in front of it which has `!` as first character in the
comment content (i.e. it starts with either `//!` or `/*!`). The string
following the `!` is the annotation until the next space. Content after
that space until the following space is an optional parameter.

The following annotations exist:

 * `string`: for `char*` fields. Tells libyaml_mapper to generate a
   (heap-allocated) null-terminated string into this field.
 * `list`: for structs containing a `data` pointer as well as two
   unsigned values `count` and `capacity`. libyaml_mapper will treat the
   annotated struct as dynamically growing list of items.
 * `tagged`: for structs containing exactly two items; the first one
   being an `enum` value and the second one being a `union`. This will
   cause the struct to be treated as [tagged union][2]. The YAML input
   value will be required to have a *local tag* matching the `repr` of
   one of the enum's values, prepended by a `!` (to be a local tag). The
   YAML value will then be deserialized into the union field with the
   same index as the specified enum value. If there are more enum values
   than union fields, the surplus values will have no content and must
   be given in the YAML as empty string with the respective local tag.
 * `repr`: takes a parameter. Currently only supported for enum values.
   Enum value will be loaded from the representation given as parameter.
   This means that the spelling of the enum value in the code will *not*
   be accepted.

## Example

Let's assume we have some header file `simple.h` like this:

```c
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
struct person_list {
  struct person* data;
  size_t count;
  size_t capacity;
};

enum int_or_string_t {
  //!repr int
  INT_VALUE,
  //!repr string
  STRING_VALUE
};

//!tagged
struct int_or_string {
  enum int_or_string_t type;
  union {
    int i;
    //!string
    char* s;
  };
};

struct root {
  char symbol;
  struct person_list people;
  struct int_or_string foo;
};

#endif
```

To autogenerate loading code, we execute this command:

    libyaml_mapper simple.h

We do not need to give any options since our root type is named `root`
(the default) and we want the generated files to be in the current
directory. The command produces `simple_loading.h` and
`simple_loading.c`.

Now, having those, we write our main procedure:

```c
#include "simple.h"
#include <simple_loading.h>

#include <yaml.h>

static const char* input =
    "symbol: W\n"
    "people:\n"
    "  - name: Ada Lovelace\n"
    "    age: 36\n"
    "    gender: female\n"
    "  - name: Karl Koch\n"
    "    age: 27\n"
    "    gender: male\n"
    "  - name: Scrooge McDuck\n"
    "    age: 75\n"
    "    gender: other\n"
    "foo: !int 42\n";

int main(int argc, char* argv[]) {
  yaml_parser_t parser;
  yaml_parser_initialize(&parser);
  yaml_parser_set_input_string(&parser, (const unsigned char*)input, strlen(input));
  struct root data;
  char* ret = load_one(&data, &parser);
  if (ret) {
    fprintf(stderr, "error while loading YAML:\n%s\n", ret);
    return 1;
  } else {
    printf("symbol = %c\nPersons:\n", data.symbol);
    for (size_t i = 0; i < data.people.count; ++i) {
      struct person* item = &data.people.data[i];
      printf("  %s, age %i\n", item->name, item->age);
    }
    yaml_parser_delete(&parser);
    return 0;
  }
}
```

Executing it yields:

    symbol = W
    Persons:
      Peter Pan, age 12
      Karl Koch, age 27
      Scrooge McDuck, age 75

## Autogenerating Code with CMake

For an example, see [test/CMakeLists.txt](test/CMakeLists.txt).

## License

[MIT](copying.txt).


 [1]: https://clang.llvm.org/doxygen/group__CINDEX.html
 [2]: https://en.wikipedia.org/wiki/Tagged_union