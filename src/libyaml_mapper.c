#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <clang-c/Index.h>

#include "../include/libyaml_mapper_common.h"

#include "cmdline_config.h"

#define MAX_NODES 2048

// -------- DFA for type names ----------

/*
 * A node of the type name DFA.
 */
typedef struct {
  /*
   * index of the following node for each possible value of char. `-1` means
   * "no follower".
   */
  int followers[256];
  /*
   * this node is a final node iff type_index != -1. In that case, type_index
   * is the index of the mapped type.
   */
  int type_index;
} typename_node_t;

/*
 * DFA for type names.
 */
typedef struct {
  /*
   * nodes of the DFA.
   */
  typename_node_t *nodes[MAX_NODES];
  /*
   * number of nodes of the DFA.
   */
  size_t count;
} typename_dfa_t;

// ---------- List of Types -------------

typedef enum {
  /*
   * Type is a value type, i.e. no pointer of any kind.
   */
  PTR_NONE,
  /*
   * Type is a string, i.e. a pointer to a null-terminated char sequence.
   */
  PTR_STRING_VALUE,
  /*
   * Type is an optional value, i.e. may be either null or point to a value.
   */
  PTR_OPTIONAL_VALUE,
  /*
   * Type is an optional string, i.e. may be either null or point to a string.
   */
  PTR_OPTIONAL_STRING_VALUE,
  /*
   * Type points to a value and may never be null.
   */
  PTR_OBJECT_POINTER
} ptr_kind;

/*
 * These flags describe the usage of a type.
 * They may be local to a struct field or similar entity.
 * Flags may be derived from annotations or the structure of the input code.
 */
typedef struct {
  /*
   * Type is a list, i.e. a struct that has data, count and capacity fields and
   * is used as dynamic list of values.
   */
  bool list;
  /*
   * Type is a tagged union, i.e. a struct that contains an enum value and a
   * union value. The enum value defines which of the union's fields is valid.
   */
  bool tagged;
  /*
   * Type is a actually a pointer to the type_descriptor_t.type. Field value
   * gets allocated during loading.
   */
  ptr_kind pointer;
} type_flags_t;

#define CONSTRUCTOR_PREFIX "static char*"
#define CONVERTER_PREFIX "static bool"
#define DESTRUCTOR_PREFIX "static void"

/*
 * Describes a type of an entity, like a struct field. In addition to the
 * actual type, this struct also holds some additional information, some of
 * which may be local to a particular usage of the base type on a certain
 * struct field.
 */
typedef struct {
  /*
   * C type of the entity. Is never a CXType_Pointer, since that is preprocessed
   * into flags (e.g. string, pointer). So this type is always a CXType_Record
   * or an atomic type.
   */
  CXType type;
  /*
   * Flags describing the usage of this particular entity.
   */
  type_flags_t flags;
  /*
   * Declaration of the constructor function, starting with CONSTRUCTOR_PREFIX.
   * May be NULL if the type does not have a constructor (only valid for
   * atomic types).
   */
  char* constructor_decl;
  /*
   * Declaration of the converter function that constructs a value of the type
   * from a string. NULL iff the type is not an atomic type.
   */
  char* converter_decl;
  /*
   * Declaration of the destructor function for the type. NULL iff the type is
   * an atomic type.
   */
  char* destructor_decl;
  /*
   * Lengths of the above function declarations.
   */
  size_t constructor_name_len, converter_name_len, destructor_name_len;
  /*
   * Spelling of the type.
   */
  const char* spelling;
} type_descriptor_t;

/*
 * List of all known types. Contains known atomic types (int, char, ...) and
 * types declared in the currently processed header file.
 */
typedef struct {
  /*
   * DFA to find types by name. The name includes the namespace (like struct).
   */
  typename_dfa_t names;
  /*
   * Dynamic list of types.
   */
  type_descriptor_t *data;
  /*
   * Number of known types in data.
   */
  size_t count;
  /*
   * Number of available slots in data.
   */
  size_t capacity;
  /*
   * Flag to signal that an error occurred during construction of this value.
   */
  bool got_error;
} types_list_t;

// -------- DFA for node fields ---------

/*
 * A node of the node field DFA.
 */
typedef struct {
  /*
   * index of the following node for each possible value of char. `-1` means
   * "no follower".
   */
  int followers[256];
  /*
   * iff this node is a final node, this contains the code to load the value of
   * the current field. NULL otherwise.
   */
  char* loader_implementation;
  /*
   * iff this node is a final node, this may contain the code to destruct the
   * value of the current field. Not every field must necessarily have code
   * for destruction. NULL otherwise.
   */
  char* destructor_implementation;
  /*
   * contains the name of the field iff this is a final node.
   */
  const char* loader_item_name;
  /*
   * true iff this is a final node and the value is declared as optional.
   */
  bool optional;
} struct_dfa_node_t;

/*
 * DFA for identifying struct fields by their name as string.
 */
typedef struct {
  /*
   * nodes of the DFA
   */
  struct_dfa_node_t* nodes[MAX_NODES];
  /*
   * Number of nodes.
   */
  size_t count;
  /*
   * Minimal and maximal value of a character used in any of the field names.
   * Useful for minimizing the resulting control table.
   */
  size_t min, max;
  /*
   * Flag that is set to true iff there was an errror during generation of the
   * DFA.
   */
  bool seen_error;
  /*
   * Known types.
   */
  const types_list_t* types_list;
} struct_dfa_t;

// ----------- Annotations --------------

/*
 * Known annotations
 */
typedef enum {
  ANN_NONE = 0,
  ANN_STRING = 1,
  ANN_LIST = 2,
  ANN_TAGGED = 3,
  ANN_REPR = 4,
  ANN_OPTIONAL = 5,
  ANN_OPTIONAL_STRING = 6,
  ANN_ENUM_END = 7
} annotation_kind_t;

/*
 * Annotation on a type or field.
 */
typedef struct {
  annotation_kind_t kind;
  /*
   * Parameter of the annotation, if the annotation supports a parameter.
   * Else NULL.
   */
  char* param;
} annotation_t;

// ---- States for discovering types ----

/*
 * State used for discovering type definitions.
 */
typedef struct {
  /*
   * List of discovered types.
   */
  types_list_t* list;
  /*
   * The last discovered type. Used to discover that a following typedef
   * contains the recent type's definition. In that case, a possible annotation
   * of the inner type will also be applied on the typedef.
   */
  CXType recent_def;
  /*
   * Annotation on the last discovered type.
   */
  annotation_t recent_annotation;
} type_info_t;

/*
 * State for discovering information on a list struct.
 */
typedef struct {
  bool seen_count, seen_capacity, seen_error;
  CXType data_type;
} list_info_t;

/*
 * Current state of tagged union discovery
 */
typedef enum {
  TAGGED_INITIAL, TAGGED_ENUM, TAGGED_UNION
} tagged_state_t;

/*
 * State for discovering information on a tagged union struct
 */
typedef struct {
  const char* enum_constants[256];
  char* destructor_calls[256];
  size_t constants_count, cur;
  bool seen_error;
  const char* field_name;
  CXType union_type;
  tagged_state_t state;
  FILE* out;
  types_list_t const* types_list;
  int enum_type_id;
} tagged_info_t;

static char const *const annotation_names[] = {
    "", "string", "list", "tagged", "repr", "optional", "optional_string"
};

static bool const annotation_has_param[] = {
    false, false, false, false, true, false, false
};

/*
 * Renders an error to stderr during execution. Uses cursor to get line and
 * column in the input where the error occurred.
 */
static void print_error(CXCursor const cursor, char const *const message, ...) {
  va_list args;
  CXSourceLocation const location = clang_getCursorLocation(cursor);
  CXString filename;
  unsigned int line, column;

  clang_getPresumedLocation(location, &filename, &line, &column);

  fprintf(stderr, "%s:%d:%d : ", clang_getCString(filename), line, column);
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
}

#define ANNOTATION_IS(value) !strncmp(start, annotation_names[value], \
                                      annotation_len[value]) && \
    (start[annotation_len[value]] == '\0' || \
     start[annotation_len[value]] == ' '  || \
     start[annotation_len[value]] == '\r' || \
     start[annotation_len[value]] == '\n' || \
     start[annotation_len[value]] == '\t')

/*
 * Get the annotation from the comment directly above the entity referred to by
 * cursor. Return true iff there was no error while parsing the annotation (that
 * includes no annotation being present), false if the annotation is unknown or
 * has the wrong number of parameters.
 *
 * Renders an error to stderr iff it returns false.
 */
static bool get_annotation(CXCursor const cursor,
                           annotation_t *const annotation) {
  static size_t annotation_len[ANN_ENUM_END] = {1};
  if (annotation_len[0] == 1) {
    // len of ANN_NONE must be 0, so the list is not properly initialized
    // yet. do this here.
    for (int i = 0; i < ANN_ENUM_END; ++i) {
      annotation_len[i] = strlen(annotation_names[i]);
    }
  }

  char const *const comment =
      clang_getCString(clang_Cursor_getRawCommentText(cursor));
  // comment starts with either "//" or "/*" and therefore comment[2] always
  // exists.
  if (comment != NULL && comment[2] == '!') {
    char const *const start = comment + 3;
    int i;
    for (i = 0; i < ANN_ENUM_END; ++i) {
      if (ANNOTATION_IS(i)) {
        annotation->kind = (annotation_kind_t) i;
        if (annotation_has_param[i]) break;
        else {
          annotation->param = NULL;
          return true;
        }
      }
    }
    if (i == ANN_ENUM_END) {
      char const *pos = start;
      while (*pos != ' ' && *pos != '\r' && *pos != '\n' && *pos != '\0') pos++;
      print_error(cursor, "unknown annotation: \"%.*s\"", pos - start, start);
      return false;
    }

    char const *pos = start;
    while (*pos != ' ' && *pos != '\r' && *pos != '\n' && *pos != '\0' &&
           *pos != '\t') pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos == '\r' || *pos == '\n' || *pos == '\0') {
      print_error(cursor, "annotation \"%.*s\" is missing its parameter!\n",
                  pos - start, start);
      return false;
    }
    char const *param_start = pos;
    do {pos++;} while (*pos != ' ' && *pos != '\r' && *pos != '\n' &&
                       *pos != '\0');
    size_t const param_len = pos - param_start;
    annotation->param = malloc(param_len + 1);
    memcpy(annotation->param, param_start, param_len);
    annotation->param[param_len] = '\0';
  } else {
    annotation->kind = ANN_NONE;
    annotation->param = NULL;
  }
  return true;
}

/*
 * Add the name to the given dfa and link it with the given target_index.
 * Return true iff that name is not already recognized by the DFA.
 */
static bool add_raw_name(typename_dfa_t *const dfa, char const *const name,
                         size_t const target_index) {
  size_t node_index = 0;
  typename_node_t *node;
  for (const char *cur = name; *cur != '\0'; ++cur) {
    if (dfa->nodes[node_index]->followers[(size_t) *cur] == -1) {
      node = malloc(sizeof(typename_node_t));
      memset(node->followers, -1, 256 * sizeof(int));
      node->type_index = -1;
      size_t const new_index = dfa->count++;
      dfa->nodes[node_index]->followers[(size_t) *cur] = (int)new_index;
      dfa->nodes[new_index] = node;
      node_index = new_index;
    } else {
      node_index = (size_t) dfa->nodes[node_index]->followers[(size_t) *cur];
    }
  }
  node = dfa->nodes[node_index];
  if (node->type_index != -1) {
    return false;
  } else {
    node->type_index = (int) target_index;
    return true;
  }
}

/*
 * Add the name of the given type to the given DFA and link it to the given
 * target_index. Return true iff the name is not already recognized by the DFA.
 */
static bool add_name(typename_dfa_t *const dfa, CXType const type,
              size_t const target_index) {
  char const *const name = clang_getCString(clang_getTypeSpelling(type));
  return add_raw_name(dfa, name, target_index);
}

/*
 * Return the index of the type with the given name in the DFA, or -1 if the
 * type name is unknown.
 */
static int find(typename_dfa_t const *const dfa, char const *const name) {
  int node_index = 0;
  for (char const *cur = name; *cur != '\0'; ++cur) {
    node_index = dfa->nodes[node_index]->followers[(size_t) *cur];
    if (node_index == -1) return -1;
  }
  return dfa->nodes[node_index]->type_index;
}

/*
 * Generate a descriptor into result of the given type, parsing its annotations.
 * Return true iff the descriptor has been generated properly. Renders an error
 * to stderr iff it returns false.
 */
static bool gen_type_descriptor(CXCursor const cursor, CXType const type,
                                type_descriptor_t *const result,
                                annotation_t const *const annotation) {
  if (annotation->kind == ANN_REPR) {
    print_error(cursor, "!repr annotation cannot be applied on %s\n",
                clang_getCString(clang_getTypeKindSpelling(type.kind)));
    free(annotation->param);
    return false;
  }

  result->type = type;
  result->flags.list = (annotation->kind == ANN_LIST);
  result->flags.tagged = (annotation->kind == ANN_TAGGED);
  result->flags.pointer = (annotation->kind == ANN_OPTIONAL) ?
      PTR_OPTIONAL_VALUE : (annotation->kind == ANN_STRING) ? PTR_STRING_VALUE :
                           (annotation->kind == ANN_OPTIONAL_STRING) ?
                           PTR_OPTIONAL_STRING_VALUE : PTR_NONE;
  return true;
}

/*
 * Return true iff the two given type descriptors point to the same type and
 * have the same flags.
 */
static bool equal_type_descriptors(type_descriptor_t const left,
                                   type_descriptor_t const right) {
  return clang_equalTypes(left.type, right.type) &&
         left.flags.list == right.flags.list &&
         left.flags.tagged == right.flags.tagged &&
         left.flags.pointer == right.flags.pointer;
}

/*
 * Add the given type at the given cursor position to the given type_info.
 * Returns the index of the added type, or -1 if the was an error while adding
 * the type.
 */
static int add_type(type_info_t* const type_info, CXType const type,
                    CXCursor const cursor) {
  annotation_t annotation;
  if (!get_annotation(cursor, &annotation)) {
    return -1;
  }

  if (type_info->list->count == type_info->list->capacity) {
    type_descriptor_t *const new_data =
        malloc(sizeof(type_descriptor_t) * type_info->list->capacity * 2);
    memcpy(new_data, type_info->list->data,
           type_info->list->count * sizeof(type_descriptor_t));
    free(type_info->list->data);
    type_info->list->data = new_data;
    type_info->list->capacity *= 2;
  }
  int const index = (int)type_info->list->count++;
  if (!gen_type_descriptor(cursor, type, &type_info->list->data[index],
                           &annotation)) {
    type_info->list->count--;
    return -1;
  }
  type_info->recent_annotation = annotation;
  type_info->recent_def = type;
  return index;
}

/*
 * Adds a predefined type with the given name and the given descriptor to the
 * given type_list. Returns the index of the type added.
 */
static size_t add_predefined(types_list_t *const types_list,
                             char const *const name,
                             type_descriptor_t const descriptor) {
  size_t const ret = types_list->count++;
  types_list->data[ret] = descriptor;
  add_raw_name(&types_list->names, name, ret);
  return ret;
}

#define TYPE_DISCOVERY_ERROR \
    do {type_info->list->got_error = true;\
        return CXChildVisit_Break; } while (false)

/*
 * Recursively walks through the defined types and adds them to the type_info
 * given by client_data.
 */
static enum CXChildVisitResult discover_types
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  if (!clang_Location_isFromMainFile(clang_getCursorLocation(cursor))) {
    return CXChildVisit_Continue;
  }
  type_info_t *const type_info = (type_info_t *) client_data;

  CXCursor const canonical = clang_getCanonicalCursor(cursor);
  if (clang_equalCursors(cursor, canonical)) {
    CXType const type = clang_getCursorType(cursor);
    char const *const type_name =
        clang_getCString(clang_getCursorSpelling(cursor));
    switch (cursor.kind) {
      case CXCursor_StructDecl:
        if (type_name[0] != '\0') {
          int const index = add_type(type_info, type, cursor);
          if (index == -1) TYPE_DISCOVERY_ERROR;
          if (!add_name(&type_info->list->names, type, (size_t)index)) {
            print_error(cursor, "duplicate type name: \"%s\"\n", type_name);
            TYPE_DISCOVERY_ERROR;
          }
        }
        return CXChildVisit_Recurse;
      case CXCursor_EnumDecl: {
        if (type_name[0] != '\0') {
          int const index = add_type(type_info, type, cursor);
          if (index == -1) TYPE_DISCOVERY_ERROR;
          if (!add_name(&type_info->list->names, type, (size_t)index)) {
            print_error(cursor, "duplicate type name: \"%s\"\n", type_name);
            TYPE_DISCOVERY_ERROR;
          }
        }
        break;
      }
      case CXCursor_FieldDecl: {
        CXCursor const type_decl =
            clang_getTypeDeclaration(clang_getCanonicalType(type));
        char const *const struct_name =
            clang_getCString(clang_getCursorSpelling(type_decl));
        if (type_decl.kind == CXCursor_StructDecl && struct_name[0] == '\0') {
          print_error(cursor, "Anonymous %s not supported!\n",
              clang_getCursorKindSpelling(type_decl.kind));
          TYPE_DISCOVERY_ERROR;
        }
        break;
      }
      case CXCursor_TypedefDecl: {
        CXType const canonical_type = clang_getCanonicalType(type);
        char const *const underlying_name = clang_getCString(
            clang_getTypeSpelling(canonical_type));
        int target_index;
        if (underlying_name[0] != '\0') {
          int const underlying_index = find(&type_info->list->names,
              underlying_name);
          if (underlying_index != -1) {
            if (clang_equalTypes(type_info->recent_def,
                                 clang_getCanonicalType(type))) {
              target_index = underlying_index;
            } else {
              type_descriptor_t descriptor;
              annotation_t annotation;
              if (!get_annotation(cursor, &annotation))
                TYPE_DISCOVERY_ERROR;
              if (!gen_type_descriptor(cursor, clang_getCanonicalType(type),
                                       &descriptor, &annotation))
                TYPE_DISCOVERY_ERROR;
              if (equal_type_descriptors(
                  type_info->list->data[underlying_index],
                  descriptor)) {
                target_index = underlying_index;
                type_info->list->data[target_index].type = type;
              } else {
                target_index = add_type(type_info, type, cursor);
              }
            }
          } else {
            target_index = add_type(type_info, type, cursor);
          }
        } else {
          target_index = add_type(type_info, type, cursor);
        }
        if (target_index == -1) TYPE_DISCOVERY_ERROR;
        if (!add_name(&type_info->list->names, type, (size_t)target_index)) {
          print_error(cursor, "duplicate type name: \"%s\"\n", type_name);
          TYPE_DISCOVERY_ERROR;
        }
        return CXChildVisit_Continue;
      }
      case CXCursor_UnionDecl:
        if (type_name[0] != '\0') {
          print_error(cursor, "named unions not supported: \"%s\"", type_name);
          TYPE_DISCOVERY_ERROR;
        } else {
          return CXChildVisit_Recurse;
        }
      default:
        print_error(cursor, "unsupported element: \"%s\"\n",
                    clang_getCursorKindSpelling(cursor.kind));
        TYPE_DISCOVERY_ERROR;
    }
  } else {
    return CXChildVisit_Continue;
  }
  return CXChildVisit_Continue;
}

/*
 * Write declarations of constructors, destructors and converters of the types
 * in the given list to the given file.
 */
static void write_decls(types_list_t const *const list, FILE *const out) {
  static const char constructor_template[] =
      CONSTRUCTOR_PREFIX " construct_%s(%s* const value, "
      "yaml_parser_t* const parser, yaml_event_t* cur)";
  static const char elaborated_constructor_template[] =
      CONSTRUCTOR_PREFIX " construct_%.*s__%s(%s* const value, "
      "yaml_parser_t* const parser, yaml_event_t* cur)";
  static const char destructor_template[] =
      DESTRUCTOR_PREFIX " delete_%s(%s* const value)";
  static const char elaborated_destructor_template[] =
      DESTRUCTOR_PREFIX " delete_%.*s__%s(%s* const value)";
  for (size_t i = 0; i < list->count; ++i) {
    if (list->data[i].type.kind == CXType_Unexposed) {
      // predefined type; do not generate anything
      continue;
    }
    char const *const type_name =
        clang_getCString(clang_getTypeSpelling(list->data[i].type));
    const char *const space = strchr(type_name, ' ');
    if (space == NULL) {
      list->data[i].constructor_name_len =
          strlen(type_name) + sizeof("construct_") - 1;
      list->data[i].constructor_decl =
          malloc(sizeof(constructor_template) - 4 + strlen(type_name) * 2);
      sprintf(list->data[i].constructor_decl, constructor_template,
              type_name, type_name);

      if (clang_getCanonicalType(list->data[i].type).kind == CXType_Enum) {
        list->data[i].destructor_name_len = 0;
        list->data[i].destructor_decl = NULL;
      } else {
        list->data[i].destructor_name_len =
            strlen(type_name) + sizeof("delete_") - 1;
        list->data[i].destructor_decl =
            malloc(sizeof(destructor_template) - 4 + strlen(type_name) * 2);
        sprintf(list->data[i].destructor_decl, destructor_template,
                type_name, type_name);
      }
    } else {
      list->data[i].constructor_name_len =
          strlen(type_name) + sizeof("construct_");
      list->data[i].constructor_decl =
          malloc(sizeof(elaborated_constructor_template) - 8 +
                 strlen(type_name) * 2 - 1);
      sprintf(list->data[i].constructor_decl, elaborated_constructor_template,
              (int)(space - type_name), type_name, space + 1, type_name);

      if (clang_getCanonicalType(list->data[i].type).kind == CXType_Enum) {
        list->data[i].destructor_name_len = 0;
        list->data[i].destructor_decl = NULL;
      } else {
        list->data[i].destructor_name_len =
            strlen(type_name) + sizeof("delete_");
        list->data[i].destructor_decl =
            malloc(sizeof(elaborated_destructor_template) - 8 +
                   strlen(type_name) * 2 - 1);
        sprintf(list->data[i].destructor_decl, elaborated_destructor_template,
                (int) (space - type_name), type_name, space + 1, type_name);
      }
    }
    fputs(list->data[i].constructor_decl, out);
    fputs(";\n", out);
    if (list->data[i].destructor_decl != NULL) {
      fputs(list->data[i].destructor_decl, out);
      fputs(";\n", out);
    }

    if (list->data[i].type.kind == CXType_Enum) {
      static const char converter_template[] =
          CONVERTER_PREFIX " convert_to_%s(const char* const value, "
          "%s* const result)";
      static const char elaborated_converter_template[] =
          CONVERTER_PREFIX " convert_to_%.*s__%s(const char* const value, "
          "%s* const result)";
      if (space == NULL) {
        list->data[i].converter_name_len = strlen(type_name) +
            sizeof("convert_to_") + 1;
        list->data[i].converter_decl =
            malloc(sizeof(converter_template) - 4 + strlen(type_name) * 2);
        sprintf(list->data[i].converter_decl, converter_template, type_name,
                type_name);
      } else {
        list->data[i].converter_name_len =
            strlen(type_name) + sizeof("construct_") + 1;
        list->data[i].converter_decl =
            malloc(sizeof(elaborated_converter_template) - 8 +
                   strlen(type_name) * 2 - 1);
        sprintf(list->data[i].converter_decl, elaborated_converter_template,
                (int)(space - type_name), type_name, space + 1, type_name);
      }
      fputs(list->data[i].converter_decl, out);
      fputs(";\n", out);
    } else {
      list->data[i].converter_decl = NULL;
      list->data[i].converter_name_len = 0;
    }
  }
}

#define LIST_VISITOR_ERROR \
    do {list_info->seen_error = true; return CXChildVisit_Break; } while (false)

/*
 * Collects info about a list struct into the list_info_t given by client_data.
 */
static enum CXChildVisitResult list_visitor
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  list_info_t *const list_info = (list_info_t*) client_data;
  const enum CXCursorKind kind = clang_getCursorKind(cursor);
  switch (kind) {
    case CXCursor_StructDecl:
      return CXChildVisit_Continue;
    case CXCursor_FieldDecl: break;
    default:
      print_error(cursor, "Unexpected item in struct (expected field): %s",
                  clang_getCString(clang_getCursorKindSpelling(kind)));
      LIST_VISITOR_ERROR;
  }
  char const *const name = clang_getCString(clang_getCursorSpelling(cursor));
  CXType const t = clang_getCanonicalType(clang_getCursorType(cursor));

  annotation_t annotation;
  get_annotation(cursor, &annotation);
  if (annotation.kind != ANN_NONE) {
    print_error(cursor, "list fields may not carry annotations!\n");
    if (annotation_has_param[annotation.kind]) free(annotation.param);
    LIST_VISITOR_ERROR;
  }

  if (!strcmp(name, "data")) {
    if (t.kind != CXType_Pointer) {
      print_error(cursor, "data field of list must be a pointer!\n");
      LIST_VISITOR_ERROR;
    }
    const CXType pointee = clang_getPointeeType(t);
    if (pointee.kind == CXType_Pointer) {
      print_error(cursor, "pointer to pointer not supported as list!\n");
      LIST_VISITOR_ERROR;
    }
    list_info->data_type = pointee;
  } else if (!strcmp(name, "count")) {
    if (t.kind != CXType_UChar && t.kind != CXType_UShort &&
        t.kind != CXType_UInt && t.kind != CXType_ULong &&
        t.kind != CXType_ULongLong) {
      print_error(cursor,
                  "\"count\" field must be an unsigned type (found %i)!\n",
                  t.kind);
      LIST_VISITOR_ERROR;
    }
    list_info->seen_count = true;
  } else if (!strcmp(name, "capacity")) {
    if (t.kind != CXType_UChar && t.kind != CXType_UShort &&
        t.kind != CXType_UInt && t.kind != CXType_ULong &&
        t.kind != CXType_ULongLong) {
      print_error(cursor, "\"capacity\" field must be an unsigned type!\n");
      LIST_VISITOR_ERROR;
    }
    list_info->seen_capacity = true;
  } else {
    print_error(cursor, "illegal field \"%s\" for list!\n", name);
    LIST_VISITOR_ERROR;
  }
  return CXChildVisit_Continue;
}

/*
 * render the call to the destructor of the given type and return it as string.
 * The subject shall contain the expression referencing the value to destruct.
 * Does not render anything if the type does not have a destructor and returns
 * NULL in that case.
 *
 * The caller shall deallocate the returned string.
 */
static char* render_destructor_call
    (type_descriptor_t const *const type_descriptor,
     char const *const subject, bool const is_ref) {
  size_t chars_needed = 1; // terminator
  size_t const subject_len = strlen(subject);
  if (type_descriptor->destructor_decl != NULL) {
    chars_needed += type_descriptor->destructor_name_len + subject_len + 4;
  }
  if (type_descriptor->flags.pointer != PTR_NONE) {
    chars_needed += sizeof("free();") - 1 + subject_len;
  }
  if (chars_needed == 1) return NULL;
  char *const ret = malloc(chars_needed);
  char *cur = ret;
  if (type_descriptor->destructor_decl != NULL) {
    cur += sprintf(ret, "%.*s(%s%s);",
        (int)type_descriptor->destructor_name_len,
        type_descriptor->destructor_decl + sizeof(DESTRUCTOR_PREFIX),
        (type_descriptor->flags.pointer != PTR_NONE || is_ref) ? "" : "&",
                   subject);
  }
  if (type_descriptor->flags.pointer != PTR_NONE) {
    sprintf(cur, "free(%s);", subject);
  }
  return ret;
}

/*
 * Generate constructor and destructior implementations for the given list.
 */
bool gen_list_impls(type_descriptor_t const *const type_descriptor,
                    types_list_t const *const types_list,
                    FILE *const out) {
  CXCursor const decl = clang_getTypeDeclaration(type_descriptor->type);
  fprintf(out, "\n%s {\n", type_descriptor->constructor_decl);
  list_info_t info = {.seen_error = false, .seen_capacity = false,
                      .seen_count = false};
  info.data_type.kind = CXType_Unexposed;
  clang_visitChildren(decl, &list_visitor, &info);
  if (info.seen_error) return false;

  if (info.data_type.kind == CXType_Unexposed) {
    print_error(decl, "data field for list missing!\n");
    return false;
  }
  if (!info.seen_count) {
    print_error(decl, "count field for list missing!\n");
    return false;
  }
  if (!info.seen_capacity) {
    print_error(decl, "capacity field for list missing!\n");
    return false;
  }
  char const *const complete_name =
      clang_getCString(clang_getTypeSpelling(info.data_type));
  int const type_index = find(&types_list->names, complete_name);
  if (type_index == -1) {
    print_error(clang_getTypeDeclaration(info.data_type), "Unknown type!");
    return false;
  }
  type_descriptor_t const *const inner_type =
      &types_list->data[type_index];

  fprintf(out,
          "  if (cur->type != YAML_SEQUENCE_START_EVENT) {\n"
          "    return wrong_event_error(YAML_SEQUENCE_START_EVENT, cur);\n"
          "  }\n"
          "  value->data = malloc(16 * sizeof(%s));\n"
          "  value->count = 0;\n"
          "  value->capacity = 16;\n"
          "  yaml_event_t event;\n"
          "  yaml_parser_parse(parser, &event);\n"
          "  while (event.type != YAML_SEQUENCE_END_EVENT) {\n"
          "    %s* item;\n"
          "    APPEND(value, item);\n"
          "    char* ret = %.*s(item, parser, &event);\n"
          "    yaml_event_delete(&event);\n"
          "    if (ret) {\n"
          "      value->count--;\n",
          complete_name, complete_name,
          (int)inner_type->constructor_name_len,
          inner_type->constructor_decl + sizeof(CONSTRUCTOR_PREFIX));
  char *const destructor_call =
      render_destructor_call(type_descriptor, "value", true);
  if (destructor_call != NULL) {
    fprintf(out, "    %s\n", destructor_call);
    free(destructor_call);
  }
  fputs("      return ret;\n"
          "    }\n"
          "    yaml_parser_parse(parser, &event);\n"
          "  }\n"
          "  yaml_event_delete(&event);\n"
          "  return NULL;\n}\n", out);

  if (type_descriptor->type.kind != CXType_Unexposed) {
    fprintf(out, "%s {\n", type_descriptor->destructor_decl);
    if (inner_type->type.kind != CXType_Unexposed) {
      fputs("  for(size_t i = 0; i < value->count; ++i) {\n", out);
      char *const inner_destructor_call =
          render_destructor_call(inner_type, "value->data[i]", false);
      if (inner_destructor_call != NULL) {
        fprintf(out, "    %s\n", inner_destructor_call);
        free(inner_destructor_call);
      }
      fputs("  }\n", out);
    }
    fputs("  free(value->data);\n}\n", out);
  }
  return true;
}

/*
 * Render a call to the given constructor, deserializing a value into the given
 * field, using the given event as starting point.
 *
 * The return value shall be deallocated by the caller.
 */
static char* new_deserialization
    (const char *const field, char const *const constructor,
     size_t const constructor_len, char const *const event_ref,
     bool const is_pointer) {
  static const char template[] =
      "ret = %.*s(%svalue->%s, parser, %s);\n";
  size_t const tmpl_len = sizeof(template) - 1;

  size_t const res_len = tmpl_len + strlen(field) + constructor_len +
                         strlen(event_ref) - 7 + (is_pointer ? 0 : 1);
  char *const ret = malloc(res_len);
  sprintf(ret, template, (int)constructor_len, constructor,
          is_pointer ? "" : "&", field, event_ref);
  return ret;
}

/*
 * Render a call to the constructor of the given underlying type, deserializing
 * a value into the given field, using the given event as starting point.
 *
 * The return value shall be deallocated by the caller.
 */
static char* gen_deserialization(char const *const name,
                                 type_descriptor_t const *const type_descriptor,
                                 char const *const event_ref) {
  return new_deserialization
      (name, type_descriptor->constructor_decl + sizeof(CONSTRUCTOR_PREFIX),
       type_descriptor->constructor_name_len, event_ref,
       type_descriptor->flags.pointer != PTR_NONE);
}

/*
 * Generate a type descriptor for the given struct field. Return true iff the
 * descripter has been successfully generated. Renders the error to stderr iff
 * it returns false.
 */
static bool describe_field(CXCursor const cursor,
                           types_list_t const *const types_list,
                           type_descriptor_t* const ret) {
  CXType const t = clang_getCanonicalType(clang_getCursorType(cursor));
  annotation_t annotation;
  bool success = get_annotation(cursor, &annotation);
  if (!success) return false;
  ptr_kind pointer_kind = PTR_OBJECT_POINTER;
  ptr_kind str_pointer_kind = PTR_STRING_VALUE;
  switch (annotation.kind) {
    case ANN_OPTIONAL_STRING:
      if (t.kind != CXType_Pointer) {
        print_error(cursor,
                    "!optional_string must be applied on a pointer type.");
        return false;
      }
      str_pointer_kind = PTR_OPTIONAL_STRING_VALUE;
      // intentional fall-through
    case ANN_STRING: {
      if (t.kind != CXType_Pointer) {
        print_error(cursor, "'!%s' must be applied a char pointer "
                            "(found on a '%s')!\n",
                    annotation_names[annotation.kind],
                    clang_getCString(clang_getTypeKindSpelling(t.kind)));
        return false;
      }
      const CXType pointee = clang_getPointeeType(t);
      if (pointee.kind != CXType_Char_S) {
        print_error(cursor, "'!%s' must be applied on a char pointer "
                            "(found on a '%s')!\n",
                    annotation_names[annotation.kind],
                    clang_getCString(clang_getTypeKindSpelling(t.kind)));
        return false;
      } else {
        ret->flags.list = false;
        ret->flags.tagged = false;
        ret->flags.pointer = str_pointer_kind;
        ret->constructor_decl = NULL;
        ret->constructor_name_len = 0;
        ret->destructor_decl = NULL;
        ret->destructor_name_len = 0;
        ret->converter_decl = NULL;
        ret->converter_name_len = 0;
        return true;
      }
    }
    case ANN_OPTIONAL:
      if (t.kind != CXType_Pointer) {
        print_error(cursor, "!optional must be applied on a pointer type.");
        return false;
      }
      pointer_kind = PTR_OPTIONAL_VALUE;
      // intentional fall-through
    case ANN_NONE:
      if (t.kind == CXType_Pointer) {
        CXType const pointee = clang_getPointeeType(t);
        if (pointee.kind == CXType_Pointer) {
          print_error(cursor, "pointer to pointer not supported.");
          return false;
        }
        char const *const type_name = clang_getCString(
            clang_getTypeSpelling(pointee));
        int const type_index = find(&types_list->names, type_name);
        if (type_index == -1) {
          print_error(cursor, "Unknown type: %s", type_name);
          return false;
        }
        *ret = types_list->data[type_index];
        ret->flags.pointer = pointer_kind;
        ret->spelling = type_name;
        return true;
      } else {
        char const *const type_name =
            clang_getCString(clang_getTypeSpelling(t));
        int const type_index = find(&types_list->names, type_name);
        if (type_index == -1) {
          print_error(cursor, "Unknown type: %s", type_name);
          return false;
        }
        *ret = types_list->data[type_index];
        ret->spelling = type_name;
        return true;
      }
    default:
      print_error(cursor, "Annotation '%s' not valid here.",
                  annotation_names[annotation.kind]);
      return false;
  }
}

/*
 * Render code that generates a value for the given field, including allocation
 * (if the field is a pointer) and a call to the constructor of the underlying
 * type.
 */
static char* gen_field_deserialization
    (char const *const name, type_descriptor_t const *const descriptor,
     char const *const event_ref) {
  switch (descriptor->flags.pointer) {
    case PTR_STRING_VALUE:
    case PTR_OPTIONAL_STRING_VALUE:
        return new_deserialization(name, "construct_string",
                                   sizeof("construct_string") - 1,
                                   event_ref, false);
    case PTR_OBJECT_POINTER:
    case PTR_OPTIONAL_VALUE: {
      char *const value_deserialization =
          gen_deserialization(name, descriptor, event_ref);
      if (value_deserialization == NULL) return NULL;
      size_t const value_deser_len = strlen(value_deserialization);
      static char const malloc_templ[] =
          "value->%s = malloc(sizeof(%s));\n          %s"
          "          if (ret != NULL) free(value->%s);\n";
      size_t const full_len = sizeof(malloc_templ) - 8 + value_deser_len +
                              strlen(name) * 2 + strlen(descriptor->spelling);
      char *const buffer = malloc(full_len);
      sprintf(buffer, malloc_templ, name, descriptor->spelling,
              value_deserialization, name);
      free(value_deserialization);
      return buffer;
    }
    default:
      return gen_deserialization(name, descriptor, event_ref);
  }
}

/*
 * Discover the names of the enum constants for a tagged union type.
 */
static enum CXChildVisitResult tagged_enum_visitor
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  tagged_info_t *const info = (tagged_info_t*) client_data;
  enum CXCursorKind const kind = clang_getCursorKind(cursor);
  if (kind != CXCursor_EnumConstantDecl) {
    print_error(cursor,
                "Unexpected item in enum type (expected enum constant): %s",
                clang_getCString(clang_getCursorKindSpelling(kind)));
    info->constants_count = SIZE_MAX;
    return CXChildVisit_Break;
  }
  char const *const name = clang_getCString(clang_getCursorSpelling(cursor));
  info->enum_constants[info->constants_count++] = name;
  return CXChildVisit_Continue;
}

#define TAGGED_VISITOR_ERROR \
    do {info->seen_error = true; return CXChildVisit_Break; } while (false)

/*
 * Discover the fields of the union of a tagged union type.
 */
static enum CXChildVisitResult tagged_union_visitor
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  tagged_info_t *const info = (tagged_info_t*) client_data;

  enum CXCursorKind const kind = clang_getCursorKind(cursor);
  switch (kind) {
    case CXCursor_StructDecl:
      return CXChildVisit_Continue;
    case CXCursor_FieldDecl: break;
    default:
      print_error(cursor, "Unexpected item in struct (expected field): %s",
                  clang_getCString(clang_getCursorKindSpelling(kind)));
      TAGGED_VISITOR_ERROR;
  }

  if (info->cur == info->constants_count) {
    print_error(cursor, "More union items than enum values!\n");
    TAGGED_VISITOR_ERROR;
  }

  const char* const name = clang_getCString(clang_getCursorSpelling(cursor));
  type_descriptor_t descriptor;
  if (!describe_field(cursor, info->types_list, &descriptor)) {
    TAGGED_VISITOR_ERROR;
  }

  char *const impl = gen_field_deserialization(name, &descriptor, "cur");
  if (!impl) {
    TAGGED_VISITOR_ERROR;
  } else {
    size_t const cur_index = info->cur++;
    fprintf(info->out, "    case %s:\n      %s      break;\n",
            info->enum_constants[cur_index], impl);
    free(impl);

    char *const accessor = malloc(sizeof("value->") + strlen(name));
    sprintf(accessor, "value->%s", name);
    info->destructor_calls[cur_index] =
        render_destructor_call(&descriptor, accessor, false);
    free(accessor);
    return CXChildVisit_Continue;
  }
}

/*
 * Discover the fields of a tagged union struct.
 */
static enum CXChildVisitResult tagged_visitor
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  CXType const t = clang_getCanonicalType(clang_getCursorType(cursor));
  tagged_info_t *const info = (tagged_info_t*)client_data;
  switch(info->state) {
    case TAGGED_INITIAL:
      if (t.kind != CXType_Enum) {
        print_error(cursor, "first field of tagged union must be an enum, "
                            "found a %s!\n",
                    clang_getCString(clang_getTypeSpelling(t)));
        TAGGED_VISITOR_ERROR;
      }
      char const *const enum_name = clang_getCString(clang_getTypeSpelling(t));
      info->enum_type_id = find(&info->types_list->names, enum_name);
      if (info->enum_type_id == -1) {
        print_error(cursor, "cannot use this enum as discriminant: "
                            "not declared in this header!\n");
        TAGGED_VISITOR_ERROR;
      }

      info->constants_count = 0;
      info->state = TAGGED_ENUM;
      clang_visitChildren(clang_getTypeDeclaration(t), &tagged_enum_visitor,
                          info);
      if (info->constants_count == 0) {
        print_error(cursor,
                    "enum for tagged union must have at least one item!\n");
        TAGGED_VISITOR_ERROR;
      }
      info->field_name = clang_getCString(clang_getCursorSpelling(cursor));
      break;
    case TAGGED_ENUM:
      if (t.kind != CXType_Record) {
        print_error(cursor, "second field of tagged union must be a union, "
                            "found a %s!\n",
                    clang_getCString(clang_getTypeSpelling(t)));
        TAGGED_VISITOR_ERROR;
      }
      fputs("  yaml_char_t* tag;\n"
            "  switch(cur->type) {\n"
            "    case YAML_SCALAR_EVENT:\n"
            "      tag = cur->data.scalar.tag;\n"
            "      break;\n"
            "    case YAML_MAPPING_START_EVENT:\n"
            "      tag = cur->data.mapping_start.tag;\n"
            "      break;\n"
            "    case YAML_SEQUENCE_START_EVENT:\n"
            "      tag = cur->data.sequence_start.tag;\n"
            "      break;\n"
            "    default:\n"
            "      return render_error(cur, \"expected tagged event, got %s\","
            "14, event_spelling(cur->type));\n"
            "  }\n"
            "  if (tag[0] != '!' || tag[1] == '\\0') {\n"
            "    return render_error(cur, \"value for tagged union must have"
            " specific local tag, got \\\"%s\\\"\", strlen((const char*)tag),"
            " (const char*)tag);\n"
            "  }\n", info->out);
      type_descriptor_t* enum_descriptor =
          &info->types_list->data[info->enum_type_id];
      fprintf(info->out,
              "  bool res = %.*s((const char*)(tag + 1), &value->%s);\n",
              (int)enum_descriptor->converter_name_len,
              enum_descriptor->converter_decl + sizeof(CONVERTER_PREFIX),
              info->field_name);
      fputs("  if (!res) {\n"
            "    return render_error(cur, \"not a valid tag: \\\"%s\\\"\","
            " strlen((const char*)tag), (const char*)tag);\n"
            "  }\n"
            "  char* ret = NULL;\n", info->out);
      fprintf(info->out, "  switch(value->%s) {\n", info->field_name);
      info->state = TAGGED_UNION;
      info->cur = 0;
      info->union_type = t;
      clang_visitChildren(clang_getTypeDeclaration(t), &tagged_union_visitor,
                          info);
      break;
    case TAGGED_UNION:
      print_error(cursor, "tagged union must not have more than two fields!\n");
      TAGGED_VISITOR_ERROR;
  }
  return CXChildVisit_Continue;
}

/*
 * Render destructor calls for the fields of a tagged union struct's union.
 */
static enum CXChildVisitResult tagged_destructor_visitor
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  (void)cursor;
  tagged_info_t *const info = (tagged_info_t*)client_data;

  size_t const index = info->cur++;
  char *const destructor = info->destructor_calls[index];
  if (destructor == NULL) {
    fprintf(info->out, "    case %s: break;\n", info->enum_constants[index]);
  } else {
    fprintf(info->out, "    case %s:\n      %s\n      break;\n",
            info->enum_constants[index], destructor);
    free(destructor);
  }
  return CXChildVisit_Continue;
}

/*
 * Write constructor and destructor implementations for the given tagged union
 * type to the given file.
 */
static bool gen_tagged_impls
    (type_descriptor_t const *const type_descriptor,
     types_list_t const *const types_list, FILE *const out) {
  CXCursor const decl = clang_getTypeDeclaration(type_descriptor->type);
  fprintf(out, "\n%s {\n", type_descriptor->constructor_decl);
  tagged_info_t info = {.constants_count = 0, .cur = 0, .seen_error = false,
                        .out = out, .types_list = types_list, .enum_type_id=-1};
  memset(info.enum_constants, 0, 256);
  memset(info.destructor_calls, 0, 256);
  clang_visitChildren(decl, &tagged_visitor, &info);
  if (info.seen_error) return false;
  bool seen_empty_variants = false;
  while (info.cur < info.constants_count) {
    seen_empty_variants = true;
    fprintf(out, "    case %s:\n", info.enum_constants[info.cur++]);
  }
  if (seen_empty_variants) {
    fputs("      if (cur->type != YAML_SCALAR_EVENT ||\n"
          "          (cur->data.scalar.value[0] != '\\0')) {\n"
          "        ret = render_error(cur, \"tag %s does not allow content\","
          " strlen((const char*)tag), (const char*)tag);\n"
          "      } else ret = NULL;\n", out);
  }
  fputs("  }\n"
        "  return ret;\n}\n", out);

  fprintf(out, "\n%s {\n  switch(value->%s) {\n",
          type_descriptor->destructor_decl, info.field_name);
  info.cur = 0;
  clang_visitChildren(clang_getTypeDeclaration(info.union_type),
                      &tagged_destructor_visitor, &info);
  for(; info.cur < info.constants_count; ++info.cur) {
    fprintf(out, "    case %s: break;\n", info.enum_constants[info.cur]);
  }
  fputs("  }\n}\n", out);
  return true;
}

/*
 * Create a new node for a struct's field name DFA
 */
static inline struct_dfa_node_t* new_node(char const *const name) {
  struct_dfa_node_t* const val = malloc(sizeof(struct_dfa_node_t));
  memset(val->followers, -1, sizeof(val->followers));
  val->loader_implementation = NULL;
  val->destructor_implementation = NULL;
  val->loader_item_name = name;
  val->destructor_implementation = NULL;
  val->optional = false;
  return val;
}

/*
 * Add a field name to the given DFA and return a pointer to the newly created
 * final node. Return NULL iff the DFA has no more available node slots.
 */
static struct_dfa_node_t* include_name(struct_dfa_t* const dfa,
                                       char const *const name) {
  struct_dfa_node_t* cur_node = dfa->nodes[0];
  for (unsigned char const* cur_char = (unsigned char*)name; *cur_char != '\0';
       ++cur_char) {
    size_t const index = (size_t)(*cur_char);
    int node_id = cur_node->followers[index];
    if (node_id == -1) {
      node_id = (int) dfa->count++;
      if (node_id == MAX_NODES) {
        fputs("too many nodes in DEA!\n", stderr);
        return NULL;
      }
      cur_node->followers[index] = node_id;
      dfa->nodes[node_id] = new_node(name);
      dfa->max = (dfa->max < index) ? index : dfa->max;
      dfa->min = (dfa->min > index) ? index : dfa->min;
    }
    cur_node = dfa->nodes[node_id];
  }
  return cur_node;
}

/*
 * Discover the fields of a struct.
 */
static enum CXChildVisitResult field_visitor
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  struct_dfa_t *const dea = (struct_dfa_t*)client_data;
  enum CXCursorKind const kind = clang_getCursorKind(cursor);
  switch (kind) {
    case CXCursor_StructDecl:
      return CXChildVisit_Continue;
    case CXCursor_FieldDecl: break;
    default:
      print_error(cursor, "Unexpected item in struct (expected field): %s",
                  clang_getCString(clang_getCursorKindSpelling(kind)));
      dea->seen_error = true;
      return CXChildVisit_Break;
  }
  char const *const name = clang_getCString(clang_getCursorSpelling(cursor));
  type_descriptor_t descriptor;
  if (!describe_field(cursor, dea->types_list, &descriptor)) {
    dea->seen_error = true;
    return CXChildVisit_Break;
  }

  struct_dfa_node_t *const cur_node = include_name(dea, name);
  if (!cur_node) {
    dea->seen_error = true;
    return CXChildVisit_Break;
  }

  cur_node->loader_implementation =
      gen_field_deserialization(name, &descriptor, "&event");
  if (cur_node->loader_implementation == NULL) {
    dea->seen_error = true;
    return CXChildVisit_Break;
  }

  char *const accessor = malloc(strlen(name) + sizeof("value->"));
  sprintf(accessor, "value->%s", name);
  cur_node->destructor_implementation =
      render_destructor_call(&descriptor, accessor, false);
  free(accessor);

  cur_node->optional = descriptor.flags.pointer == PTR_OPTIONAL_VALUE ||
      descriptor.flags.pointer == PTR_OPTIONAL_STRING_VALUE;
  return CXChildVisit_Continue;
}

/*
 * Render a control table to map field names given as string to field indexes.
 */
static void put_control_table(struct_dfa_t const *const dea, FILE *const out) {
  fprintf(out, "  static const int8_t table[][%zu] = {\n",
          dea->max - dea->min + 3);
  for(size_t i = 0; i < dea->count; ++i) {
    fputs("      {", out);
    for(size_t j = dea->min - 1; j <= dea->max + 1; ++j) {
      if (j > dea->min - 1) fputs(", ", out);
      fprintf(out, "%d", dea->nodes[i]->followers[j]);
    }
    if (i < dea->count - 1) fputs("},\n", out);
    else fputs("}\n", out);
  }
  fputs("  };\n", out);
}

/*
 * Render the code to process the value for every possible given field.
 */
static void process_struct_loaders(struct_dfa_t const *const dea,
                                   FILE *const out) {
  size_t index = 0;
  for (size_t i = 0; i < dea->count; i++) {
    if (dea->nodes[i]->loader_implementation != NULL) {
      fprintf(out,
              "      case %zu:\n"
              "        if (found[%zu]) {\n"
              "          size_t escaped_len;\n"
              "          char* escaped = escape(name, &escaped_len);\n"
              "          ret = render_error(&key, \"duplicate key: %%s\", "
              "escaped_len, escaped);\n"
              "          free(escaped);\n"
              "        } else {\n"
              "          found[%zu] = true;\n"
              "          ", i, index, index);
      fputs(dea->nodes[i]->loader_implementation, out);
      fprintf(out, "          if (ret != NULL) found[%zu] = false;\n"
                   "        }\n"
                   "        break;\n", index);
      index++;
    }
  }
}

/*
 * Render destructor calls for all fields that may already have been constructed
 * before an error has been encountered.
 */
static void process_struct_cleanup(struct_dfa_t const *const dea,
                                   FILE *const out) {
  size_t index = 0;
  for (size_t i = 0; i < dea->count; i++) {
    if (dea->nodes[i]->destructor_implementation != NULL) {
      fprintf(out,
              "    if (found[%zu]) {\n"
              "      %s\n"
              "    }\n", index++, dea->nodes[i]->destructor_implementation);
    }
  }
}

/*
 * Render destructor calls for all fields of the struct for the struct's
 * destructor.
 */
static void process_struct_destructors(struct_dfa_t const *const dea,
                                       FILE *const out) {
  for (size_t i = 0; i < dea->count; ++i) {
    if (dea->nodes[i]->destructor_implementation != NULL) {
      fputs("\n  ", out);
      fputs(dea->nodes[i]->destructor_implementation, out);
    }
  }
}

/*
 * Write implementations of the given struct type's constructor an destructor
 * to the given file.
 */
bool gen_struct_impls(type_descriptor_t const *const type_descriptor,
                      types_list_t const *const types_list, FILE *const out) {
  CXCursor const decl =
      clang_getTypeDeclaration(clang_getCanonicalType(type_descriptor->type));
  struct_dfa_t dea = {.count=1, .min=255, .max=0, .seen_error=false,
                      .types_list=types_list};
  dea.nodes[0] = new_node(NULL);
  clang_visitChildren(decl, &field_visitor, &dea);
  if (dea.seen_error) {
    for (int i = 0; i < dea.count; ++i) {
      if (dea.nodes[i]->loader_implementation != NULL) {
        free(dea.nodes[i]->loader_implementation);
      }
      free(dea.nodes[i]);
    }
    return false;
  }

  fprintf(out, "\n%s {\n", type_descriptor->constructor_decl);
  put_control_table(&dea, out);
  fputs("  if (cur->type != YAML_MAPPING_START_EVENT) {\n"
        "    return wrong_event_error(YAML_MAPPING_START_EVENT, cur);\n"
        "  }\n"
        "  yaml_event_t key;\n"
        "  yaml_parser_parse(parser, &key);\n"
        "  char* ret = NULL;\n"
        "  bool found[] = {", out);
  bool first = true;
  for (size_t i = 0; i < dea.count; i++) {
    if (dea.nodes[i]->loader_implementation != NULL) {
      if (first) first = false;
      else fputs(", ", out);
      fputs("false", out);
    }
  }
  fputs("};\n"
        "  static const bool optional[] = {", out);
  first = true;
  for (size_t i = 0; i < dea.count; i++) {
    if (dea.nodes[i]->loader_implementation != NULL) {
      if (first) first = false;
      else fputs(", ", out);
      fputs(dea.nodes[i]->optional ? "true" : "false", out);
    }
  }
  fputs("};\n", out);
  for (size_t i = 0; i < dea.count; i++) {
    if (dea.nodes[i]->optional) {
      fprintf(out, "  value->%s = NULL;\n", dea.nodes[i]->loader_item_name);
    }
  }
  fputs("  static char const *const names[] = {", out);
  first = true;
  for (size_t i = 0; i < dea.count; i++) {
    if (dea.nodes[i]->loader_implementation != NULL) {
      if (first) first = false;
      else fputs(", ", out);
      fprintf(out, "\"%s\"", dea.nodes[i]->loader_item_name);
    }
  }
  fputs("};\n"
        "  while(key.type != YAML_MAPPING_END_EVENT) {\n"
        "    if (key.type != YAML_SCALAR_EVENT) {\n"
        "      ret = wrong_event_error(YAML_SCALAR_EVENT, &key);\n"
        "      break;\n"
        "    }\n"
        "    int8_t result;\n"
        "    walk(table, key.data.scalar.value, ", out);
  fprintf(out, "%zu, %zu, result);\n", dea.min - 1, dea.max + 1);
  fputs("    yaml_event_t event;\n"
        "    yaml_parser_parse(parser, &event);\n"
        "    const char* const name = (const char*)key.data.scalar.value;\n"
        "    switch(result) {\n", out);
  process_struct_loaders(&dea, out);
  fputs("      default: {\n"
        "          size_t escaped_len;\n"
        "          char* escaped = escape(name, &escaped_len);\n"
        "          ret = render_error(&key, \"unknown field: %s\", escaped_len,"
        "escaped);\n"
        "          free(escaped);\n"
        "        }\n"
        "        break;\n"
        "    }\n"
        "    yaml_event_delete(&event);\n"
        "    if (ret != NULL) break;\n"
        "    yaml_event_delete(&key);\n"
        "    yaml_parser_parse(parser, &key);\n"
        "  }\n"
        "  yaml_event_delete(&key);\n"
        "  if (!ret) {\n"
        "    for (size_t i = 0; i < sizeof(found); i++) {\n"
        "      if (!found[i] && !optional[i]) {\n"
        "        const size_t name_len = strlen(names[i]);\n"
        "        ret = render_error(cur, \"missing value for field \\\"%s\\\"\","
        " name_len, names[i]);\n"
        "        break;\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  if (ret) {\n", out);
  process_struct_cleanup(&dea, out);
  fputs("  }\n"
        "  return ret;\n}\n", out);

  fprintf(out, "\n%s {", type_descriptor->destructor_decl);
  process_struct_destructors(&dea, out);
  fputs("\n}\n", out);

  for (size_t i = 0; i < dea.count; i++) {
    if (dea.nodes[i]->loader_implementation != NULL) {
      free(dea.nodes[i]->loader_implementation);
    }
    if (dea.nodes[i]->destructor_implementation != NULL) {
      free(dea.nodes[i]->destructor_implementation);
    }
    free(dea.nodes[i]);
  }

  return true;
}

/*
 * Discovers the constants of an enum type.
 */
static enum CXChildVisitResult enum_visitor
    (CXCursor const cursor, CXCursor const parent,
     CXClientData const client_data) {
  (void)parent;
  struct_dfa_t *const dea = (struct_dfa_t*)client_data;

  enum CXCursorKind const kind = clang_getCursorKind(cursor);
  if (kind != CXCursor_EnumConstantDecl) {
    print_error(cursor,
                "Unexpected item in enum type (expected enum constant): %s",
                clang_getCString(clang_getCursorKindSpelling(kind)));
    dea->seen_error = true;
    return CXChildVisit_Break;
  }
  char const *const name = clang_getCString(clang_getCursorSpelling(cursor));

  annotation_t annotation;
  bool success = get_annotation(cursor, &annotation);
  if (!success) {
    dea->seen_error = true;
    return CXChildVisit_Break;
  }
  char const *representation = name;

  switch (annotation.kind) {
    case ANN_REPR:
      representation = annotation.param;
      break;
    case ANN_NONE:
      break;
    default:
      print_error(cursor, "Unsupported annotation for enum constant: '%s'",
                  annotation_names[annotation.kind]);
      dea->seen_error = true;
      return CXChildVisit_Break;
  }

  struct_dfa_node_t *const cur_node = include_name(dea, representation);
  if (annotation.kind == ANN_REPR) free(annotation.param);
  if (!cur_node) {
    dea->seen_error = true;
    return CXChildVisit_Break;
  }

  static char const template[] = "*result = %s;\n";
  size_t const impl_len = sizeof(template) + strlen(name) - 1;
  char *const impl = malloc(impl_len);
  sprintf(impl, template, name);
  cur_node->loader_implementation = impl;
  return CXChildVisit_Continue;
}

/*
 * Render handling of possible enum constants and deallocate the nodes in the
 * DFA.
 */
static void process_enum_nodes(struct_dfa_t const *const dfa, FILE *const out) {
  for (size_t i = 0; i < dfa->count; i++) {
    if (dfa->nodes[i]->loader_implementation != NULL) {
      fprintf(out,
              "      case %zu:\n"
              "        ", i);
      fputs(dfa->nodes[i]->loader_implementation, out);
      free(dfa->nodes[i]->loader_implementation);
      fputs("          break;\n", out);
    }
    free(dfa->nodes[i]);
  }
}

/*
 * Writes the constructor and converter implementations of the given enum type
 * to the given file.
 */
bool gen_enum_impls
    (type_descriptor_t const *const type_descriptor,
     types_list_t const *const types_list, FILE *const out) {
  CXCursor const decl = clang_getTypeDeclaration(type_descriptor->type);
  struct_dfa_t dea = {.count=1, .min=255, .max=0, .seen_error=false,
                      .types_list=types_list};
  dea.nodes[0] = new_node(NULL);
  clang_visitChildren(decl, &enum_visitor, &dea);
  if (dea.seen_error) {
    for (int i = 0; i < dea.count; ++i) {
      if (dea.nodes[i]->loader_implementation != NULL) {
        free(dea.nodes[i]->loader_implementation);
      }
      free(dea.nodes[i]);
    }
    return false;
  }
  fprintf(out, "%s {\n", type_descriptor->converter_decl);
  put_control_table(&dea, out);
  fputs("  int8_t res;\n"
        "  walk(table, (unsigned char*)value, ", out);
  fprintf(out, "%zu, %zu, res);\n", dea.min - 1, dea.max + 1);
  fputs("  switch(res) {\n", out);
  process_enum_nodes(&dea, out);
  fputs("    default: return false;\n"
        "  }\n"
        "  return true;\n"
        "}\n\n", out);

  fprintf(out, "%s {\n", type_descriptor->constructor_decl);
  fputs("  (void)parser;\n"
        "  if (cur->type != YAML_SCALAR_EVENT) {\n"
        "    return wrong_event_error(YAML_SCALAR_EVENT, cur);\n"
        "  }\n"
        "  char* ret;\n", out);
  fprintf(out,
        "  if (%.*s((const char*)cur->data.scalar.value, value)) {\n",
          (int)type_descriptor->converter_name_len,
          type_descriptor->converter_decl + sizeof(CONVERTER_PREFIX));
  fputs("    ret = NULL;\n"
        "  } else {\n"
        "    size_t escaped_len;\n"
        "    char* escaped = escape((const char*)cur->data.scalar.value, "
        "&escaped_len);\n"
        "    ret = render_error(cur, \"unknown enum value: %s\", escaped_len,"
        " escaped);\n"
        "    free(escaped);\n"
        "  }\n"
        "  return ret;\n"
        "}\n\n", out);
  return true;
}

/*
 * Write implemenations of constructors, destructors and converters for all
 * known types into the given file.
 */
static bool write_impls(types_list_t const *const list,
                        FILE *const out) {
  for (size_t i = 0; i < list->count; ++i) {
    type_descriptor_t const *const type_descriptor = &list->data[i];
    if (type_descriptor->type.kind == CXType_Unexposed) continue;
    switch(clang_getCanonicalType(type_descriptor->type).kind) {
      case CXType_Record:
        if (type_descriptor->flags.list) {
          if (!gen_list_impls(type_descriptor, list, out)) return false;
        } else if (type_descriptor->flags.tagged) {
          if (!gen_tagged_impls(type_descriptor, list, out)) return false;
        } else {
          if (!gen_struct_impls(type_descriptor, list, out)) return false;
        }
        break;
      case CXType_Enum:
        if (!gen_enum_impls(type_descriptor, list, out)) return false;
        break;
      default: {
        const CXCursor decl = clang_getTypeDeclaration(type_descriptor->type);
        print_error(decl, "Unexpected type item: %s\n",
                    clang_getCString(clang_getCursorKindSpelling(decl.kind)));
        return false;
      }
    }
  }
  return true;
}

/*
 * Set flags of the given descriptor to the values predefined types have.
 */
static void mark_as_predefined(type_descriptor_t *const descriptor) {
  descriptor->type.kind = CXType_Unexposed;
  descriptor->flags.tagged = false;
  descriptor->flags.list = false;
  descriptor->flags.pointer = PTR_NONE;
  descriptor->converter_name_len = 0;
  descriptor->converter_decl = NULL;
}

#define KNOWN_TYPE(name, constructor) {\
  (void)&(constructor); /* ensure constructor exists */\
  type_descriptor_t desc = {\
    .constructor_decl = "static char* " #constructor,\
    .constructor_name_len = sizeof(#constructor),\
    .destructor_decl = NULL, .destructor_name_len = 0};\
  mark_as_predefined(&desc);\
  add_predefined(&types_list, #name, desc);\
}

int main(int const argc, char const *argv[]) {
  cmdline_config_t config;
  switch (process_cmdline_args(argc, argv, &config)) {
    case ARGS_ERROR: return 1;
    case ARGS_HELP: return 0;
    case ARGS_SUCCESS: break;
  }

  char const *const args[] = {"-std=c11"};
  CXIndex index = clang_createIndex(0, 1);
  CXTranslationUnit unit =
      clang_parseTranslationUnit(index, config.input_file_path, args, 1,
                                 NULL, 0, CXTranslationUnit_None);
  if (unit == NULL) {
    fprintf(stderr, "Unable to parse '%s'.\n", config.input_file_path);
    return 1;
  }

  CXCursor const cursor = clang_getTranslationUnitCursor(unit);

  types_list_t types_list = {.data = malloc(sizeof(type_descriptor_t) * 64),
      .count = 0, .capacity = 64, .got_error = false};
  types_list.names.count = 1;
  types_list.names.nodes[0] = malloc(sizeof(typename_node_t));
  memset(types_list.names.nodes[0]->followers, -1, 256 * sizeof(int));
  types_list.names.nodes[0]->type_index = -1;

  // known types
  KNOWN_TYPE(short, construct_short);
  KNOWN_TYPE(int, construct_int);
  KNOWN_TYPE(long, construct_long);
  KNOWN_TYPE(long long, construct_long_long);

  KNOWN_TYPE(unsigned char, construct_unsigned_char);
  KNOWN_TYPE(unsigned short, construct_unsigned_short);
  KNOWN_TYPE(unsigned, construct_unsigned);
  KNOWN_TYPE(unsigned long, construct_unsigned_long);
  KNOWN_TYPE(unsigned long long, construct_unsigned_long_long);

  KNOWN_TYPE(float, construct_float);
  KNOWN_TYPE(double, construct_double);
  KNOWN_TYPE(long double, construct_long_double);

  KNOWN_TYPE(char, construct_char);
  KNOWN_TYPE(_Bool, construct_bool);

  type_info_t type_info = {.list = &types_list};
  type_info.recent_annotation.kind = ANN_NONE;
  type_info.recent_annotation.param = NULL;
  type_info.recent_def.kind = CXType_Unexposed;
  clang_visitChildren(cursor, &discover_types, &type_info);
  if (type_info.recent_annotation.param != NULL)
    free(type_info.recent_annotation.param);

  if (types_list.got_error) {
    return 1;
  }
  int root_index = find(&types_list.names, config.root_name);
  if (root_index == -1) {
    fprintf(stderr, "Did not find root type '%s'.\n", config.root_name);
    return 1;
  }
  type_descriptor_t const *const root_type = &types_list.data[root_index];

  FILE *const out_impl = fopen(config.output_impl_path, "w");
  if (out_impl == NULL) {
    fprintf(stderr, "Unable to open '%s' for writing.\n",
            config.output_impl_path);
    return 1;
  }
  fprintf(out_impl,
          "#include <libyaml_mapper_common.h>\n"
          "#include <stdbool.h>\n"
          "#include <locale.h>\n"
          "#include \"%s\"\n", config.output_header_name);

  write_decls(&types_list, out_impl);
  if (!write_impls(&types_list, out_impl)) return 1;

  char const *const type_spelling =
      clang_getCString(clang_getTypeSpelling(root_type->type));
  fprintf(out_impl,
          "char* load_one(%s* value, yaml_parser_t* parser) {\n"
          "  char* old_locale = setlocale(LC_NUMERIC, NULL);\n"
          "  setlocale(LC_NUMERIC, \"C\");\n"
          "  yaml_event_t event;\n"
          "  yaml_parser_parse(parser, &event);\n"
          "  if (event.type == YAML_STREAM_START_EVENT) {\n"
          "    yaml_event_delete(&event);\n"
          "    yaml_parser_parse(parser, &event);\n"
          "  }\n"
          "  if (event.type != YAML_DOCUMENT_START_EVENT) {\n"
          "    yaml_event_delete(&event);\n"
          "    return wrong_event_error(YAML_DOCUMENT_START_EVENT, &event);\n"
          "  }\n"
          "  yaml_event_delete(&event);\n"
          "  yaml_parser_parse(parser, &event);\n"
          "  char* ret = %.*s(value, parser, &event);\n"
          "  yaml_event_delete(&event);\n"
          "  yaml_parser_parse(parser, &event); // assume document end\n"
          "  yaml_event_delete(&event);\n"
          "  setlocale(LC_NUMERIC, old_locale);\n"
          "  return ret;\n"
          "}\n", type_spelling,
          (int)root_type->constructor_name_len,
          root_type->constructor_decl + sizeof(CONSTRUCTOR_PREFIX));
  char* const destructor_call =
      render_destructor_call(root_type, "value", true);
  fprintf(out_impl,
          "void free_one(%s* value) {\n"
          "  %s\n"
          "}\n", type_spelling, destructor_call == NULL ? "" : destructor_call);
  if (destructor_call != NULL) free(destructor_call);
  fclose(out_impl);

  FILE *const header_out = fopen(config.output_header_path, "w");
  if (header_out == NULL) {
    fprintf(stderr, "unable to open '%s' for writing.\n",
            config.output_header_path);
    return 1;
  }
  fprintf(header_out,
          "#include <yaml.h>\n"
          "#include <%s>\n"
          "char* load_one(%s* value, yaml_parser_t* parser);\n"
          "void free_one(%s* value);\n",
          config.input_file_name, type_spelling, type_spelling);
  fclose(header_out);

  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
}