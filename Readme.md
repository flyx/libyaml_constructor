# libyaml_constructor

**libyaml_constructor** is a tool and library that loads a YAML document into
native C types. It consists of the *generator* tool that takes a C header and
generates the code for loading, and the *runtime* library that provides common
functionality used by the generated code.

The *constructor* depends on the excellent [libclang][1] for parsing the
given header. The generated code and the *runtime* (quite obviously) depend on
[libyaml][4].

## Status

I develop this project as part of a larger one. That means that it
probably will not get complete support for every feature the C type
system has in the forseeable future. I only add features when needed.

Since this project is in development, do not expect it to work smoothly
and be wary about its generated code, especially concerning memory leaks
when the YAML fails to load.

List of stuff that currently works:

 * integer and unsigned types (`short`, `int`, `long`, `long long`,
   `unsigned char`, `unsigned short`, `unsigned`, `unsigned long`,
   `unsigned long long`)
 * floating point types (`float`, `double`, `long double`)
 * `char` (interpreted as ASCII-character)
 * `bool` (taking the literals `true` and `false`)
 * `struct` with fields containing anything from this list
 * `enum` at top-level
 * `char*` as string or optional string
 * pointers to anything on this list apart from pointers, possibly optional
 * dynamic lists (see below)
 * [tagged unions][2] (see below)
 * having reference to line and column in error messages

List of stuff that currently does not work:

 * serializing back to YAML
 * anonymous structs inside structs
 * reading the documentation (there is none apart from this Readme)

## Usage

    yaml_constructor_generator [options] file
      options:
        -o directory       writes output files to $directory (default: .)
        -r name            expects the root type to be named $name.
                           default: "root"
        -n name            names output files $name.h and $name.c .
                           default: ${file without ext}_loading.{h,c}.

In your code, you need to *annotate* certain structures so that
libyaml_constructor knows your intention. You annotate a type or field by
adding a comment in front of it which has `!` as first character in the
comment content (i.e. it starts with either `//!` or `/*!`). The string
following the `!` is the annotation until the next space. Content after
that space until the following space is an optional parameter.

The following annotations exist:

 * `string`: for `char*` fields. Tells the generator to generate a
   (heap-allocated) null-terminated string into this field.
 * `list`: for structs containing a `data` pointer as well as two
   unsigned values `count` and `capacity`. The generator will treat the
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
 * `optional`: for fields of pointer types. Tells the generator that this field
   may be omitted in the YAML, in which case it will be `NULL` after loading.
 * `optional_string`: for `char*` fields, works like `optional` but if given,
   parses into a null-terminated string.

## Building

To build `libyaml_constructor_generator`, you need:

 * A **C** compiler. **libyaml_constructor** is tested and known to work with
   GCC, LLVM/Clang and Visual Studio.
 * [CMake][3], Version 3.10 or later
 * [libclang][1]

The tests, as any generated code, also depend on [libyaml][4].

### Instructions for Windows

Download the latest [Visual Studio IDE][6] (Community Edition unless you are in
possession of a license) and install it. Download the latest
[pre-built LLVM binaries][5] for Windows and execute the installer. Download
the latest [CMake][3] installer and execute it.

Since you are already in possession of all tools required to compile libyaml,
I suggest you build it yourself: Get the latest [LibYaml release][7] and unpack
it somewhere. Ignore the build instructions on the page; we will use CMake to
build it instead. Start the CMake GUI, configure the source code to be the
directory you unpacked the files into, and the binary folder to something like
`cmake-vs` inside the source folder. Click *Configure*, *Generate* and then
*Open Project*. Visual Studio should launch. Select the *Release*
configuration, right-Click on the *yaml* project and select *Build*. This
should give you a *yaml.dll* in `cmake-vs/Release`.

Now, to compile `yaml_constructor_generator`, go back to the CMake GUI and
select the `libyaml_constructor` folder as source folder and a subfolder
`cmake-vs` as subfolder. We will need to add some entries to the configuration
so that CMake finds all dependencies:

 * Add a `PATH` variable named `LibClang_ROOT` and make it point to your LLVM
   installation's root folder (e.g. `C:\LLVM`).
 * Add a `PATH` variable named `LibYaml_ROOT` and make it point to the folder
   you unpacked the libyaml sources to.
 * Add a `PATH` variable named `LibYaml_LIBDIR` and make it point to the folder
   that holds the `yaml.dll` (e.g. `${LibYaml_ROOT}/cmake-vs/Release`).

After that, you should be able to *Configure* and *Generate* a Visual Studio
project. Open it, select the *Release* configuration and build the
*yaml_constructor_generator* project. It generates the executable and places
required dependencies (`libclang.dll`) in the output folder
(`cmake-vs/Release`). You can now use it to generate code.

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

    yaml_constructor_generator simple.h

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
  char* ret = load_one_struct__root(&data, &parser);
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
    free_one_struct__root(&data);
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

For an example, see [test/CMakeLists.txt](test/CMakeLists.txt). Link the target
that uses the generated code to the `yaml_constructor` target, which is the
runtime.

I suggest using git submodules or [git-subrepo][8] to use libyaml_constructor in
your project. That will allow you to reuse this project's CMake module to
find libyaml.

## License

[MIT](copying.txt)


 [1]: https://clang.llvm.org/doxygen/group__CINDEX.html
 [2]: https://en.wikipedia.org/wiki/Tagged_union
 [3]: https://cmake.org/
 [4]: https://pyyaml.org/wiki/LibYAML
 [5]: https://releases.llvm.org/download.html
 [6]: https://visualstudio.microsoft.com/
 [7]: https://pyyaml.org/wiki/LibYAML
 [8]: https://github.com/ingydotnet/git-subrepo