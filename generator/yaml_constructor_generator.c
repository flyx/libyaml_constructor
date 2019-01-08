#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <clang-c/Index.h>
#include <assert.h>
#include <inttypes.h>

#include "cmdline_config.h"

#define MAX_NODES 2048

// -------- DFA for type names ----------

/*
 * A node of the type name DFA.
 */
typedef struct {
  /*
   * index of the following node for each possible value of char. `UINT16_MAX`
   * means "no follower".
   */
  uint16_t followers[256];
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

typedef enum {
  NO_DEFAULT,
  DEFAULT_INT,
  DEFAULT_FLOAT,
  DEFAULT_LIST,
  DEFAULT_BOOL,
  DEFAULT_ENUM
} default_kind;

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
   * Type is custom, i.e. has a constructor and destructor defined by the user.
   * constructor and destructor must be declared in the input.
   */
  bool custom;
  /*
   * Type has a default value, i.e. it is allowed to leave out a value for a
   * field of this type, and that field will then take the default value.
   */
  default_kind default_value;
  /*
   * Type is a actually a pointer to the type_descriptor_t.type. Field value
   * gets allocated during loading.
   */
  ptr_kind pointer;
} type_flags_t;

#define CONSTRUCTOR_PREAMBLE "bool"
#define CONVERTER_PREAMBLE "static bool"
#define DESTRUCTOR_PREAMBLE "void"
#define LOADER_PREFIX "yaml_load_"
#define DEALLOCATOR_PREFIX "yaml_free_"
#define CONSTRUCTOR_PREFIX "yaml_construct_"
#define CONVERTER_PREFIX "convert_to_"
#define DESTRUCTOR_PREFIX "yaml_delete_"

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
   * Declaration of the constructor function, starting with CONSTRUCTOR_PREAMBLE.
   * May be NULL if the type does not have a constructor (only valid for
   * atomic types).
   */
  char *constructor_decl;
  /*
   * Declaration of the converter function that constructs a value of the type
   * from a string. NULL iff the type is not an atomic type.
   */
  char *converter_decl;
  /*
   * Declaration of the destructor function for the type. NULL iff the type is
   * an atomic type.
   */
  char *destructor_decl;
  /*
   * Lengths of the above function declarations.
   */
  size_t constructor_name_len, converter_name_len, destructor_name_len;
  /*
   * Spelling of the type.
   */
  const char *spelling;
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

typedef enum {
  REQUIRED, OPTIONAL, HAS_DEFAULT
} occurrence_kind;

/*
 * A node of the node field DFA.
 */
typedef struct {
  /*
   * index of the following node for each possible value of char. `UINT16_MAX`
   * means "no follower".
   */
  uint16_t followers[256];
  /*
   * iff this node is a final node, this contains the code to load the value of
   * the current field. NULL otherwise.
   */
  char *loader_implementation;
  /*
   * iff this node is a final node, this may contain the code to destruct the
   * value of the current field. Not every field must necessarily have code
   * for destruction. NULL otherwise.
   */
  char *destructor_implementation;
  /*
   * iff this value is a final node, this may contain assignments that need to
   * be made in case the field is not assigned a value. That assignments are a
   * list and NULL-terminated.
   */
  char **default_implementation;
  /*
   * contains the name of the field iff this is a final node.
   */
  const char *loader_item_name;
} struct_dfa_node_t;

/*
 * DFA for identifying struct fields by their name as string.
 */
typedef struct {
  /*
   * nodes of the DFA
   */
  struct_dfa_node_t *nodes[MAX_NODES];
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
  const types_list_t *types_list;
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
  ANN_IGNORED = 7,
  ANN_CUSTOM = 8,
  ANN_DEFAULT = 9,
  ANN_ENUM_END = 10
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
  char *param;
} annotation_t;

typedef struct {
  char const **data;
  size_t count, capacity;
} names_list_t;

// ---- States for discovering types ----

/*
 * State used for discovering type definitions.
 */
typedef struct {
  /*
   * List of discovered types.
   */
  types_list_t *list;
  /*
   * List of discovered names of custom constructors and destructors
   */
  names_list_t constructor_names, destructor_names;
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
  const char *enum_constants[256];
  char *destructor_calls[256];
  size_t constants_count, cur;
  bool seen_error;
  const char *field_name;
  CXType union_type;
  tagged_state_t state;
  FILE *out;
  types_list_t const *types_list;
  int enum_type_id;
} tagged_info_t;

static char const *const annotation_names[] = {
    "", "string", "list", "tagged", "repr", "optional", "optional_string",
    "ignored", "custom", "default"
};

static bool const annotation_has_param[] = {
    false, false, false, false, true, false, false, false, false
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
    char const *param_start = pos;
    if (*pos == '\r' || *pos == '\n' || *pos == '\0') {
      annotation->param = NULL;
    } else {
      do { pos++; } while (*pos != ' ' && *pos != '\r' && *pos != '\n' &&
        *pos != '\0');
      size_t const param_len = pos - param_start;
      annotation->param = malloc(param_len + 1);
      memcpy(annotation->param, param_start, param_len);
      annotation->param[param_len] = '\0';
    }
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
    if (dfa->nodes[node_index]->followers[(size_t) *cur] == UINT16_MAX) {
      node = malloc(sizeof(typename_node_t));
      memset(node->followers, UINT8_MAX, 256 * sizeof(uint16_t));
      node->type_index = -1;
      size_t const new_index = dfa->count++;
      assert(new_index < UINT16_MAX);
      dfa->nodes[node_index]->followers[(size_t) *cur] = (uint16_t)new_index;
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
  uint16_t node_index = 0;
  for (char const *cur = name; *cur != '\0'; ++cur) {
    node_index = dfa->nodes[node_index]->followers[(size_t) *cur];
    if (node_index == UINT16_MAX) return -1;
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
  result->flags.custom = (annotation->kind == ANN_CUSTOM);
  result->flags.pointer = (annotation->kind == ANN_OPTIONAL) ?
      PTR_OPTIONAL_VALUE : (annotation->kind == ANN_STRING) ? PTR_STRING_VALUE :
                           (annotation->kind == ANN_OPTIONAL_STRING) ?
                           PTR_OPTIONAL_STRING_VALUE : PTR_NONE;
  result->spelling = clang_getCString(clang_getTypeSpelling(type));
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
         left.flags.custom == right.flags.custom &&
         left.flags.pointer == right.flags.pointer;
}

/*
 * Add the given type at the given cursor position to the given type_info.
 * Returns the index of the added type; -1 if the was an error while adding
 * the type, and -2 if the type was ignored (because it was annotated
 * with !ignore).
 */
static int add_type(type_info_t *const type_info, CXType const type,
                    CXCursor const cursor) {
  annotation_t annotation;
  if (!get_annotation(cursor, &annotation)) {
    return -1;
  }
  if (annotation.kind == ANN_IGNORED) return -2;

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

#define APPEND(list, ptr) do { \
  if ((list)->count == (list)->capacity) { \
    void* const newlist = malloc(sizeof(*(list)->data) * (list)->capacity * 2);\
    if (newlist == NULL) {\
			(ptr) = NULL;\
    } else {\
      memcpy(newlist, (list)->data, sizeof(*(list)->data) * (list)->capacity); \
      free((list)->data); \
      (list)->data = newlist; \
      (list)->capacity *= 2; \
      (ptr) = &((list)->data[(list)->count++]); \
    }\
  } else {\
	  (ptr) = &((list)->data[(list)->count++]); \
  }\
} while (false)

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

  CXType const type = clang_getCursorType(cursor);
  CXCursor const canonical = clang_getCanonicalCursor(cursor);
  bool discover_current = clang_equalCursors(cursor, canonical) != 0;
  if (!discover_current) {
    // may still need to process this type if we didn't already.
    if (!clang_Location_isFromMainFile(clang_getCursorLocation(canonical))) {
      int const type_index = find(&type_info->list->names,
                                  clang_getCString(clang_getTypeSpelling(type)));
      discover_current = type_index == -1;
    }
  }
  if (discover_current) {
    char const *const type_name =
        clang_getCString(clang_getCursorSpelling(cursor));
    switch (cursor.kind) {
      case CXCursor_StructDecl:
        if (type_name[0] != '\0') {
          annotation_t annotation;
          if (!get_annotation(cursor, &annotation))
            TYPE_DISCOVERY_ERROR;
          if (annotation.kind == ANN_IGNORED) {
            return CXChildVisit_Continue;
          } else {
            int const index = add_type(type_info, type, cursor);
            if (index == -1) TYPE_DISCOVERY_ERROR;
            if (index != -2 &&
                !add_name(&type_info->list->names, type, (size_t) index)) {
              print_error(cursor, "duplicate type name: \"%s\"\n", type_name);
              TYPE_DISCOVERY_ERROR;
            }
            // don't search for types inside custom types; they are
            // not required to be supported.
            if (annotation.kind == ANN_CUSTOM) return CXChildVisit_Continue;
          }
        }
        return CXChildVisit_Recurse;
      case CXCursor_EnumDecl: {
        if (type_name[0] != '\0') {
          int const index = add_type(type_info, type, cursor);
          if (index == -1) TYPE_DISCOVERY_ERROR;
          if (index != -2 &&
              !add_name(&type_info->list->names, type, (size_t)index)) {
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
              if (annotation.kind != ANN_IGNORED) {
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
            }
          } else {
            target_index = add_type(type_info, type, cursor);
          }
        } else {
          target_index = add_type(type_info, type, cursor);
        }
        if (target_index == -1) TYPE_DISCOVERY_ERROR;
        if (target_index != -2 &&
            !add_name(&type_info->list->names, type, (size_t)target_index)) {
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
      case CXCursor_FunctionDecl: {
        const char *name = clang_getCString(clang_getCursorSpelling(cursor));
        if (strncmp(CONSTRUCTOR_PREFIX, name,
                    sizeof(CONSTRUCTOR_PREFIX) - 1) == 0) {
          char const **ptr;
          APPEND(&type_info->constructor_names, ptr);
          if (ptr != NULL) (*ptr) = name;
          // TODO: ensure that the function is properly typed
        } else if (strncmp(DESTRUCTOR_PREFIX, name,
                           sizeof(DESTRUCTOR_PREFIX) - 1) == 0) {
          char const **ptr;
          APPEND(&type_info->destructor_names, ptr);
          if (ptr != NULL) (*ptr) = name;
          // TODO: ensure that the function is properly typed
        } else {
          print_error(cursor, "unsupported function (expected constructor or "
                              "destructor): %s\n", name);
          TYPE_DISCOVERY_ERROR;
        }
        break;
      }
      default:
        print_error(cursor, "unsupported element: \"%s\"\n",
                    clang_getCString(clang_getCursorKindSpelling(cursor.kind)));
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
static bool write_decls(type_info_t const *const info, FILE *const out) {
  const types_list_t *list = info->list;
  static const char constructor_template[] =
      CONSTRUCTOR_PREAMBLE " " CONSTRUCTOR_PREFIX "%s(%s *const value, "
      "yaml_loader_t *const loader, yaml_event_t *cur)";
  static const char elaborated_constructor_template[] =
      CONSTRUCTOR_PREAMBLE " " CONSTRUCTOR_PREFIX "%.*s_%s(%s *const value, "
      "yaml_loader_t *const loader, yaml_event_t *cur)";
  static const char destructor_template[] =
      DESTRUCTOR_PREAMBLE " " DESTRUCTOR_PREFIX "%s(%s *const value)";
  static const char elaborated_destructor_template[] =
      DESTRUCTOR_PREAMBLE " " DESTRUCTOR_PREFIX "%.*s_%s(%s *const value)";
  for (size_t i = 0; i < list->count; ++i) {
    if (list->data[i].type.kind == CXType_Unexposed) {
      // predefined type; do not generate anything
      continue;
    }
    char const *const type_name =
        clang_getCString(clang_getTypeSpelling(list->data[i].type));
    const char *const space = strchr(type_name, ' ');
    list->data[i].constructor_name_len =
        strlen(type_name) + sizeof(CONSTRUCTOR_PREFIX) - 1;
    if (space == NULL) {
      list->data[i].constructor_decl =
          malloc(sizeof(constructor_template) - 4 + strlen(type_name) * 2);
      sprintf(list->data[i].constructor_decl, constructor_template,
              type_name, type_name);

      if (clang_getCanonicalType(list->data[i].type).kind == CXType_Enum) {
        list->data[i].destructor_name_len = 0;
        list->data[i].destructor_decl = NULL;
      } else {
        list->data[i].destructor_name_len =
            strlen(type_name) + sizeof(DESTRUCTOR_PREFIX) - 1;
        list->data[i].destructor_decl =
            malloc(sizeof(destructor_template) - 4 + strlen(type_name) * 2);
        sprintf(list->data[i].destructor_decl, destructor_template,
                type_name, type_name);
      }
    } else {
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
            strlen(type_name) + sizeof(DESTRUCTOR_PREFIX) - 1;
        list->data[i].destructor_decl =
            malloc(sizeof(elaborated_destructor_template) - 8 +
                   strlen(type_name) * 2 - 1);
        sprintf(list->data[i].destructor_decl, elaborated_destructor_template,
                (int) (space - type_name), type_name, space + 1, type_name);
      }
    }
    if (list->data[i].flags.custom) {
      const char *name = list->data[i].constructor_decl +
                         sizeof(CONSTRUCTOR_PREAMBLE);
      bool found = false;
      for (size_t j = 0; j < info->constructor_names.count; ++j) {
        const size_t len = strlen(info->constructor_names.data[j]);
        if (len == list->data[i].constructor_name_len &&
            strncmp(name, info->constructor_names.data[j], len) == 0) {
          found = true;
          break;
        }
      }
      if (!found) {
        print_error(clang_getTypeDeclaration(list->data[i].type),
                    "missing constructor for custom type!\n");
        return false;
      }

      if (list->data[i].destructor_decl != NULL) {
        name = list->data[i].destructor_decl + sizeof(DESTRUCTOR_PREAMBLE);
        found = false;
        for (size_t j = 0; j < info->destructor_names.count; ++j) {
          const size_t len = strlen(info->destructor_names.data[j]);
          if (len == list->data[i].destructor_name_len &&
              strncmp(name, info->destructor_names.data[j], len) == 0) {
            found = true;
            break;
          }
        }
        if (!found) {
          print_error(clang_getTypeDeclaration(list->data[i].type),
                      "missing destructor for custom type!\n");
          return false;
        }
      }

      // don't write anything; user has declared constructor and destructor
      continue;
    }
    fputs(list->data[i].constructor_decl, out);
    fputs(";\n", out);
    if (list->data[i].destructor_decl != NULL) {
      fputs(list->data[i].destructor_decl, out);
      fputs(";\n", out);
    }
  }
  return true;
}

static void write_static_decls(types_list_t const *const list,
                               FILE *const out) {
  for (size_t i = 0; i < list->count; ++i) {
    if (list->data[i].type.kind == CXType_Unexposed) {
      // predefined type; do not generate anything
      continue;
    } else if (list->data[i].flags.custom) {
      // custom type; user has declared constructor and destructor.
      continue;
    }
    char const *const type_name =
        clang_getCString(clang_getTypeSpelling(list->data[i].type));
    const char *const space = strchr(type_name, ' ');
    if (list->data[i].type.kind == CXType_Enum) {
      static const char converter_template[] =
          CONVERTER_PREAMBLE " " CONVERTER_PREFIX "%s(const char *const value, "
          "%s *const result)";
      static const char elaborated_converter_template[] =
          CONVERTER_PREAMBLE " " CONVERTER_PREFIX
          "%.*s_%s(const char *const value, %s *const result)";
      if (space == NULL) {
        list->data[i].converter_name_len = strlen(type_name) +
                                           sizeof(CONVERTER_PREFIX) - 1;
        list->data[i].converter_decl =
            malloc(sizeof(converter_template) - 4 + strlen(type_name) * 2);
        sprintf(list->data[i].converter_decl, converter_template, type_name,
                type_name);
      } else {
        list->data[i].converter_name_len =
            strlen(type_name) + sizeof(CONVERTER_PREFIX) - 1;
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
  switch(annotation.kind) {
    case ANN_IGNORED: return CXChildVisit_Continue;
    case ANN_NONE: break;
    default:
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
static char *render_destructor_call
    (type_descriptor_t const *const type_descriptor,
     char const *const subject, bool const is_ref) {
  size_t chars_needed = 1; // terminator
  size_t const subject_len = strlen(subject);
  if (type_descriptor->destructor_decl != NULL) {
    chars_needed += type_descriptor->destructor_name_len + subject_len + 4;
  }
  if (type_descriptor->flags.pointer != PTR_NONE) {
    chars_needed += sizeof("free();") - 1 + subject_len;
    if (type_descriptor->flags.pointer == PTR_OPTIONAL_VALUE ||
        type_descriptor->flags.pointer == PTR_OPTIONAL_STRING_VALUE) {
      chars_needed += sizeof("if ( != NULL) {}") - 1 + subject_len;
    }
  }
  if (chars_needed == 1) return NULL;
  char *const ret = malloc(chars_needed);
  char *cur = ret;
  if (type_descriptor->flags.pointer == PTR_OPTIONAL_VALUE ||
      type_descriptor->flags.pointer == PTR_OPTIONAL_STRING_VALUE) {
    cur += sprintf(cur, "if (%s != NULL) {", subject);
  }
  if (type_descriptor->destructor_decl != NULL) {
    cur += sprintf(cur, "%.*s(%s%s);",
        (int)type_descriptor->destructor_name_len,
        type_descriptor->destructor_decl + sizeof(DESTRUCTOR_PREAMBLE),
        (type_descriptor->flags.pointer != PTR_NONE || is_ref) ? "" : "&",
                   subject);
  }
  if (type_descriptor->flags.pointer != PTR_NONE) {
    cur += sprintf(cur, "free(%s);", subject);
  }
  if (type_descriptor->flags.pointer == PTR_OPTIONAL_VALUE ||
      type_descriptor->flags.pointer == PTR_OPTIONAL_STRING_VALUE) {
    /*cur +=*/ sprintf(cur, "}");
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
    print_error(clang_getTypeDeclaration(info.data_type),
                "Unknown type: \"%s\"\n", complete_name);
    return false;
  }
  type_descriptor_t const *const inner_type =
      &types_list->data[type_index];

  fprintf(out,
          "  if (!yaml_constructor_check_event_type(loader, cur, "
          "YAML_SEQUENCE_START_EVENT))\n"
          "    return false;\n"
          "  value->data = malloc(16 * sizeof(%s));\n"
          "  if (value->data == NULL) {\n"
          "    loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
          "    yaml_event_delete(cur);\n"
          "    return false;\n"
          "  }\n"
          "  value->count = 0;\n"
          "  value->capacity = 16;\n"
          "  yaml_event_t event;\n"
          "  if (yaml_parser_parse(loader->parser, &event) == 0) {\n"
          "    loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
          "    yaml_event_delete(cur);\n"
          "    return false;\n"
          "  }\n"
          "  while (event.type != YAML_SEQUENCE_END_EVENT) {\n"
          "    %s *item;\n"
          "    YAML_CONSTRUCTOR_APPEND(value, item);\n"
          "    bool ret = false;\n"
          "    if (item == NULL) {\n"
          "      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
          "      yaml_event_delete(cur);\n"
          "    } else {\n"
          "      ret = %.*s(item, loader, &event);\n"
          "      if (!ret) {\n"
          "        value->count--;\n"
          "        yaml_event_delete(cur);\n"
          "      }\n"
          "    }\n"
          "    if (ret) {\n"
          "      yaml_event_delete(&event);\n"
          "      if (yaml_parser_parse(loader->parser, &event) == 0) {\n"
          "        loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
          "        yaml_event_delete(cur);\n"
          "        ret = false;\n"
          "      }\n"
          "    }\n"
          "    if (!ret) {\n",
          complete_name, complete_name,
          (int)inner_type->constructor_name_len,
          inner_type->constructor_decl + sizeof(CONSTRUCTOR_PREAMBLE));
  char *const destructor_call =
      render_destructor_call(type_descriptor, "value", true);
  if (destructor_call != NULL) {
    fprintf(out, "      %s\n", destructor_call);
    free(destructor_call);
  }
  fputs("      return false;\n"
        "    }\n"
        "  }\n"
        "  yaml_event_delete(&event);\n"
        "  return true;\n}\n", out);

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
    fputs("  if (value->data != NULL) free(value->data);\n}\n", out);
  }
  return true;
}

/*
 * Render a call to the given constructor, deserializing a value into the given
 * field, using the given event as starting point.
 *
 * The return value shall be deallocated by the caller.
 */
static char *new_deserialization
    (const char *const field, char const *const constructor,
     size_t const constructor_len, char const *const event_ref,
     bool const is_pointer) {
  static const char template[] =
      "ret = %.*s(%svalue->%s, loader, %s);\n";
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
static char *gen_deserialization(char const *const name,
                                 type_descriptor_t const *const type_descriptor,
                                 char const *const event_ref) {
  return new_deserialization
      (name, type_descriptor->constructor_decl + sizeof(CONSTRUCTOR_PREAMBLE),
       type_descriptor->constructor_name_len, event_ref,
       type_descriptor->flags.pointer != PTR_NONE);
}

enum describe_field_result_t {
  ADDED, IGNORED, ERROR
};

/*
 * Generate a type descriptor for the given struct field. Return ADDED iff the
 * descripter has been successfully generated. Renders the error to stderr iff
 * it returns ERROR. Returns IGNORED iff the field is tagged with the
 * !ignored annotation.
 */
static enum describe_field_result_t describe_field
    (CXCursor const cursor, types_list_t const *const types_list,
     type_descriptor_t *const ret) {
  CXType const t = clang_getCanonicalType(clang_getCursorType(cursor));
  annotation_t annotation;
  bool success = get_annotation(cursor, &annotation);
  if (!success) return ERROR;
  ptr_kind pointer_kind = PTR_OBJECT_POINTER;
  ptr_kind str_pointer_kind = PTR_STRING_VALUE;
  bool should_have_default = false;
  switch (annotation.kind) {
    case ANN_IGNORED: return IGNORED;
    case ANN_OPTIONAL_STRING:
      if (t.kind != CXType_Pointer) {
        print_error(cursor,
                    "!optional_string must be applied on a pointer type.");
        return ERROR;
      }
      str_pointer_kind = PTR_OPTIONAL_STRING_VALUE;
      // intentional fall-through
    case ANN_STRING: {
      if (t.kind != CXType_Pointer) {
        print_error(cursor, "'!%s' must be applied a char pointer "
                            "(found on a '%s')!\n",
                    annotation_names[annotation.kind],
                    clang_getCString(clang_getTypeKindSpelling(t.kind)));
        return ERROR;
      }
      const CXType pointee = clang_getPointeeType(t);
      if (pointee.kind != CXType_Char_S) {
        print_error(cursor, "'!%s' must be applied on a char pointer "
                            "(found on a '%s')!\n",
                    annotation_names[annotation.kind],
                    clang_getCString(clang_getTypeKindSpelling(t.kind)));
        return ERROR;
      } else {
        ret->flags.list = false;
        ret->flags.tagged = false;
        ret->flags.default_value = NO_DEFAULT;
        ret->flags.pointer = str_pointer_kind;
        ret->constructor_decl = NULL;
        ret->constructor_name_len = 0;
        ret->destructor_decl = NULL;
        ret->destructor_name_len = 0;
        ret->converter_decl = NULL;
        ret->converter_name_len = 0;
        return ADDED;
      }
    }
    case ANN_DEFAULT:
      if (t.kind == CXType_Pointer) {
        print_error(cursor, "!default may not be applied on a pointer type "
                            "(use !optional instead).");
        return ERROR;
      }
      should_have_default = true;
      break;
    case ANN_OPTIONAL:
      if (t.kind != CXType_Pointer) {
        print_error(cursor, "!optional must be applied on a pointer type.");
        return ERROR;
      }
      pointer_kind = PTR_OPTIONAL_VALUE;
      break;
    case ANN_NONE:
      break;
    default:
      print_error(cursor, "Annotation '%s' not valid here.",
                  annotation_names[annotation.kind]);
      return ERROR;
  }

  if (t.kind == CXType_Pointer) {
    CXType const pointee = clang_getPointeeType(t);
    if (pointee.kind == CXType_Pointer) {
      print_error(cursor, "pointer to pointer not supported.");
      return ERROR;
    }
    char const *const type_name = clang_getCString(
        clang_getTypeSpelling(pointee));
    int const type_index = find(&types_list->names, type_name);
    if (type_index == -1) {
      print_error(cursor, "Unknown type: %s\n", type_name);
      return ERROR;
    }
    *ret = types_list->data[type_index];
    ret->flags.pointer = pointer_kind;
    ret->flags.default_value = NO_DEFAULT;
    ret->spelling = type_name;
    return ADDED;
  } else {
    char const *const type_name =
        clang_getCString(clang_getTypeSpelling(t));
    int const type_index = find(&types_list->names, type_name);
    if (type_index == -1) {
      print_error(cursor, "Unknown type: %s\n", type_name);
      return ERROR;
    }
    *ret = types_list->data[type_index];
    if (should_have_default) {
      switch (t.kind) {
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
          ret->flags.default_value = DEFAULT_INT;
          break;
        case CXType_Float:
        case CXType_Double:
          ret->flags.default_value = DEFAULT_FLOAT;
          break;
        case CXType_Record:
          if (ret->flags.list) {
            ret->flags.default_value = DEFAULT_LIST;
          } else {
            print_error(cursor, "type of !default struct must be a list!");
            return ERROR;
          }
          break;
        case CXType_Bool:
          ret->flags.default_value = DEFAULT_BOOL;
          break;
        case CXType_Enum:
          ret->flags.default_value = DEFAULT_ENUM;
          break;
        default:
          print_error(cursor, "!default not supported for %s.",
                      clang_getCString(clang_getTypeSpelling(t)));
          return ERROR;
      }
    } else ret->flags.default_value = NO_DEFAULT;
    ret->spelling = type_name;
    return ADDED;
  }
}

/*
 * Render code that generates a value for the given field, including allocation
 * (if the field is a pointer) and a call to the constructor of the underlying
 * type.
 */
static char *gen_field_deserialization
    (char const *const name, type_descriptor_t const *const descriptor,
     char const *const event_ref) {
  switch (descriptor->flags.pointer) {
    case PTR_STRING_VALUE:
    case PTR_OPTIONAL_STRING_VALUE:
        return new_deserialization(name, "yaml_construct_string",
                                   sizeof("yaml_construct_string") - 1,
                                   event_ref, false);
    case PTR_OBJECT_POINTER:
    case PTR_OPTIONAL_VALUE: {
      char *const value_deserialization =
          gen_deserialization(name, descriptor, event_ref);
      if (value_deserialization == NULL) return NULL;
      size_t const value_deser_len = strlen(value_deserialization);
      static char const malloc_templ[] =
          "value->%s = malloc(sizeof(%s));\n          %s"
          "          if (!ret) free(value->%s);\n";
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
  if (name == NULL) {
    print_error(cursor,
                "Unexpected enum constant decl without a name!");
    info->constants_count = SIZE_MAX;
    return CXChildVisit_Break;
  }
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

  const char *const name = clang_getCString(clang_getCursorSpelling(cursor));
  type_descriptor_t descriptor;
  switch (describe_field(cursor, info->types_list, &descriptor)) {
    case ERROR:
      TAGGED_VISITOR_ERROR;
    case IGNORED:
      return CXChildVisit_Continue;
    case ADDED: break;
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
      } else if (info->constants_count == SIZE_MAX) {
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
      type_descriptor_t *enum_descriptor =
          &info->types_list->data[info->enum_type_id];
      fprintf(info->out,
              "  const char typename[] = \"%s\";\n", enum_descriptor->spelling);
      fputs("  yaml_char_t *tag;\n"
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
            "      loader->error_info.type = YAML_LOADER_ERROR_STRUCTURAL;\n"
            "      loader->error_info.event = *cur;\n"
            "      loader->error_info.expected_event_type = YAML_SCALAR_EVENT;\n"
            "      return false;\n"
            "  }\n"
            "  if (tag == NULL || tag[0] != '!' || tag[1] == '\\0') {\n"
            "    loader->error_info.expected = malloc(sizeof(typename));\n"
            "    if (loader->error_info.expected == NULL) {\n"
            "      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
            "      yaml_event_delete(cur);\n"
            "    } else {\n"
            "      loader->error_info.type = YAML_LOADER_ERROR_TAG;\n"
            "      memcpy(loader->error_info.expected, typename,"
            "             sizeof(typename));\n"
            "      loader->error_info.event = *cur;\n"
            "    }\n"
            "    return false;\n"
            "  }\n", info->out);
      fprintf(info->out,
              "  bool res = %.*s((const char*)(tag + 1), &value->%s);\n",
              (int)enum_descriptor->converter_name_len,
              enum_descriptor->converter_decl + sizeof(CONVERTER_PREAMBLE),
              info->field_name);
      fputs("  if (!res) {\n"
            "    loader->error_info.expected = malloc(sizeof(typename));\n"
            "    if (loader->error_info.expected == NULL) {\n"
            "      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
            "      yaml_event_delete(cur);\n"
            "    } else {\n"
            "      loader->error_info.type = YAML_LOADER_ERROR_TAG;\n"
            "      memcpy(loader->error_info.expected, typename,"
            "             sizeof(typename));\n"
            "      loader->error_info.event = *cur;\n"
            "    }\n"
            "    return false;\n"
            "  }\n"
            "  bool ret = false;\n", info->out);
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
          "        loader->error_info.expected = malloc(sizeof(typename));\n"
          "        if (loader->error_info.expected == NULL) {\n"
          "          loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
          "          yaml_event_delete(cur);\n"
          "        } else {\n"
          "          loader->error_info.type = YAML_LOADER_ERROR_TAG;\n"
          "          memcpy(loader->error_info.expected, typename,"
          "                 sizeof(typename));\n"
          "          loader->error_info.event = *cur;\n"
          "        }\n"
          "      } else ret = true;\n", out);
  }
  fputs("  }\n"
        "  return ret;\n}\n", out);

  fprintf(out, "\n%s {\n  switch(value->%s) {\n",
          type_descriptor->destructor_decl, info.field_name);
  for (size_t i = 0; i < info.constants_count; ++i) {
    char *const destructor = info.destructor_calls[i];
    if (destructor == NULL) {
      fprintf(info.out, "    case %s: break;\n", info.enum_constants[i]);
    } else {
      fprintf(info.out, "    case %s:\n      %s\n      break;\n",
              info.enum_constants[i], destructor);
      free(destructor);
    }
  }
  fputs("  }\n}\n", out);
  return true;
}

/*
 * Create a new node for a struct's field name DFA
 */
static inline struct_dfa_node_t *new_node(char const *const name) {
  struct_dfa_node_t *const val = malloc(sizeof(struct_dfa_node_t));
  memset(val->followers, -1, sizeof(val->followers));
  val->loader_implementation = NULL;
  val->destructor_implementation = NULL;
  val->loader_item_name = name;
  val->destructor_implementation = NULL;
  val->default_implementation = NULL;
  return val;
}

/*
 * Add a field name to the given DFA and return a pointer to the newly created
 * final node. Return NULL iff the DFA has no more available node slots.
 */
static struct_dfa_node_t *include_name(struct_dfa_t *const dfa,
                                       char const *const name) {
  struct_dfa_node_t *cur_node = dfa->nodes[0];
  for (unsigned char const *cur_char = (unsigned char*)name; *cur_char != '\0';
       ++cur_char) {
    size_t const index = (size_t)(*cur_char);
    uint16_t node_id = cur_node->followers[index];
    if (node_id == UINT16_MAX) {
      node_id = (uint16_t) dfa->count++;
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
  switch (describe_field(cursor, dea->types_list, &descriptor)) {
    case ERROR:
      dea->seen_error = true;
      return CXChildVisit_Break;
    case IGNORED:
      return CXChildVisit_Continue;
    case ADDED: break;
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

  const size_t accessor_len = strlen(name) + sizeof("value->");
  char *const accessor = malloc(accessor_len);
  sprintf(accessor, "value->%s", name);
  cur_node->destructor_implementation =
      render_destructor_call(&descriptor, accessor, false);

  if (descriptor.flags.pointer == PTR_OPTIONAL_VALUE ||
     descriptor.flags.pointer == PTR_OPTIONAL_STRING_VALUE) {
    cur_node->default_implementation = malloc(sizeof(char*) * 2);
    *cur_node->default_implementation =
        malloc(accessor_len + sizeof(" = NULL;"));
    sprintf(*cur_node->default_implementation, "%s = NULL;", accessor);
    cur_node->default_implementation[1] = NULL;
  } else {
    switch (descriptor.flags.default_value) {
      case NO_DEFAULT:
        cur_node->default_implementation = NULL;
        break;
      case DEFAULT_INT:
        cur_node->default_implementation = malloc(sizeof(char *) * 2);
        *cur_node->default_implementation =
            malloc(accessor_len + sizeof(" = 0;"));
        sprintf(*cur_node->default_implementation, "%s = 0;", accessor);
        cur_node->default_implementation[1] = NULL;
        break;
      case DEFAULT_FLOAT:
        cur_node->default_implementation = malloc(sizeof(char *) * 2);
        *cur_node->default_implementation =
            malloc(accessor_len + sizeof(" = 0.0;"));
        sprintf(*cur_node->default_implementation, "%s = 0.0;", accessor);
        cur_node->default_implementation[1] = NULL;
        break;
      case DEFAULT_BOOL:
        cur_node->default_implementation = malloc(sizeof(char *) * 2);
        *cur_node->default_implementation =
            malloc(accessor_len + sizeof(" = false;"));
        sprintf(*cur_node->default_implementation, "%s = false;", accessor);
        cur_node->default_implementation[1] = NULL;
        break;
      case DEFAULT_ENUM: {
        const char *type_spelling =
            clang_getCString(clang_getTypeSpelling(descriptor.type));
        cur_node->default_implementation = malloc(sizeof(char *) * 2);
        *cur_node->default_implementation =
            malloc(accessor_len + strlen(type_spelling) + sizeof(" = ()0;"));
        sprintf(*cur_node->default_implementation, "%s = (%s)0;",
            accessor, type_spelling);
        cur_node->default_implementation[1] = NULL;
        break;
      }
      case DEFAULT_LIST:
        cur_node->default_implementation = malloc(sizeof(char *) * 4);
        *cur_node->default_implementation =
            malloc(accessor_len + sizeof(".data = NULL;"));
        sprintf(*cur_node->default_implementation, "%s.data = NULL;",
                accessor);
        cur_node->default_implementation[1] =
            malloc(accessor_len + sizeof(".capacity = 0;"));
        sprintf(cur_node->default_implementation[1], "%s.capacity = 0;",
                accessor);
        cur_node->default_implementation[2] =
            malloc(accessor_len + sizeof(".count = 0;"));
        sprintf(cur_node->default_implementation[2], "%s.count = 0;",
                accessor);
        cur_node->default_implementation[3] = NULL;
        break;
      default:
        print_error(cursor, "internal error; illegal default value");
        return CXChildVisit_Break;
    }
  }

  free(accessor);
  return CXChildVisit_Continue;
}

/*
 * Render a control table to map field names given as string to field indexes.
 */
static void put_control_table(struct_dfa_t const *const dea, FILE *const out) {
  fprintf(out, "  static const uint16_t table[][%zu] = {\n",
          dea->max - dea->min + 3);
  for(size_t i = 0; i < dea->count; ++i) {
    fputs("      {", out);
    for(size_t j = dea->min - 1; j <= dea->max + 1; ++j) {
      if (j > dea->min - 1) fputs(", ", out);
      fprintf(out, "%"PRIu16, dea->nodes[i]->followers[j]);
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
              "          loader->error_info.expected = malloc(name_len);\n"
              "          if (loader->error_info.expected == NULL) {\n"
              "            loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
              "            yaml_event_delete(&key);\n"
              "          } else {\n"
              "            loader->error_info.type = YAML_LOADER_ERROR_DUPLICATE_KEY;\n"
              "            memcpy(loader->error_info.expected, name, name_len);\n"
              "            loader->error_info.event = key;\n"
              "          }\n"
              "          ret = false;\n"
              "        } else {\n"
              "          if (yaml_parser_parse(loader->parser, &event) == 0) {\n"
              "            loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
              "            yaml_event_delete(&key);\n"
              "            ret = false;\n"
              "          } else {\n"
              "            ", i, index);
      fputs(dea->nodes[i]->loader_implementation, out);
      fprintf(out,
              "            if (ret) {\n"
              "              yaml_event_delete(&event);\n"
              "              found[%zu] = true;\n"
              "            }\n"
              "          }\n"
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
              "    }\n", index, dea->nodes[i]->destructor_implementation);
    }
    if (dea->nodes[i]->loader_implementation != NULL) ++index;
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
  if (dea.max >= dea.min) {
    put_control_table(&dea, out);
  } else {
    free(dea.nodes[0]);
    dea.count = 0;
  }
  fputs("  if (!yaml_constructor_check_event_type(loader, cur, "
        "YAML_MAPPING_START_EVENT))\n"
        "    return false;"
        "  yaml_event_t key;\n"
        "  if (yaml_parser_parse(loader->parser, &key) == 0) {\n"
        "    loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
        "    yaml_event_delete(cur);\n"
        "    return false;\n"
        "  }\n"
        "  bool ret = true;\n", out);
  if (dea.count > 0) {
    fputs("  bool found[] = {", out);
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
        fputs(dea.nodes[i]->default_implementation == NULL ? "false" : "true",
              out);
      }
    }
    fputs("};\n", out);
    for (size_t i = 0; i < dea.count; i++) {
      if (dea.nodes[i]->default_implementation != NULL) {
        for (char **line = dea.nodes[i]->default_implementation; *line != NULL;
             ++line) {
          fprintf(out, "  %s\n", *line);
        }
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
          "    if (!yaml_constructor_check_event_type(loader, &key, "
          "YAML_SCALAR_EVENT)) {\n"
          "      ret = false;\n"
          "      break;\n"
          "    }\n"
          "    uint16_t result;\n"
          "    YAML_CONSTRUCTOR_WALK(table, key.data.scalar.value, ", out);
    fprintf(out, "%zu, %zu, result);\n", dea.min - 1, dea.max + 1);
    fputs("    yaml_event_t event;\n"
          "    const char *const name = (const char*)key.data.scalar.value;\n"
          "    const size_t name_len = strlen(name) + 1;\n"
          "    switch(result) {\n", out);
    process_struct_loaders(&dea, out);
    fputs("      default: {\n"
          "        loader->error_info.expected = malloc(name_len);\n"
          "        if (loader->error_info.expected == NULL) {\n"
          "          loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
          "          yaml_event_delete(&key);\n"
          "        } else {\n"
          "          loader->error_info.type = YAML_LOADER_ERROR_UNKNOWN_KEY;\n"
          "          memcpy(loader->error_info.expected, name, name_len);\n"
          "          loader->error_info.event = key;\n"
          "        }\n"
          "        ret = false;\n"
          "        break;\n"
          "      }\n"
          "    }\n"
          "    if (!ret) break;\n"
          "    yaml_event_delete(&key);\n"
          "    if (yaml_parser_parse(loader->parser, &key) == 0) {\n"
          "      loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
          "      ret = false;\n"
          "      break;\n"
          "    }\n"
          "  }\n", out);
  } else {
    fputs("  if (!yaml_constructor_check_event_type(loader, &key, "
          "YAML_MAPPING_END_EVENT)) {\n"
          "    yaml_event_delete(cur);\n"
          "    return false;\n"
          "  }\n", out);
  }
  if (dea.count > 0) {
    fputs("  if (ret) {\n"
          "    yaml_event_delete(&key);\n"
          "    for (size_t i = 0; i < sizeof(found); i++) {\n"
          "      if (!found[i] && !optional[i]) {\n"
          "        const size_t missing_len = strlen(names[i]) + 1;\n"
          "        loader->error_info.expected = malloc(missing_len);\n"
          "        if (loader->error_info.expected == NULL) {\n"
          "          loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
          "          yaml_event_delete(cur);\n"
          "        } else {\n"
          "          loader->error_info.type = YAML_LOADER_ERROR_MISSING_KEY;\n"
          "          memcpy(loader->error_info.expected, names[i], missing_len);\n"
          "          loader->error_info.event = *cur;\n"
          "        }\n"
          "        ret = false;\n"
          "        break;\n"
          "      }\n"
          "    }\n"
          "  } else yaml_event_delete(cur);\n"
          "  if (!ret) {\n", out);
    process_struct_cleanup(&dea, out);
    fputs("  }\n", out);
  }
  fputs("  return ret;\n}\n", out);

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
    case ANN_IGNORED:
      return CXChildVisit_Continue;
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
  fputs("  uint16_t res;\n"
        "  YAML_CONSTRUCTOR_WALK(table, (unsigned char*)value, ", out);
  fprintf(out, "%zu, %zu, res);\n", dea.min - 1, dea.max + 1);
  fputs("  switch(res) {\n", out);
  process_enum_nodes(&dea, out);
  fputs("    default: return false;\n"
        "  }\n"
        "  return true;\n"
        "}\n\n", out);

  fprintf(out, "%s {\n", type_descriptor->constructor_decl);
  fputs("  (void)loader;\n"
        "  if (!yaml_constructor_check_event_type(loader, cur, "
        "YAML_SCALAR_EVENT))\n"
        "    return false;\n", out);
  fprintf(out,
        "  if (%.*s((const char*)cur->data.scalar.value, value)) {\n",
          (int)type_descriptor->converter_name_len,
          type_descriptor->converter_decl + sizeof(CONVERTER_PREAMBLE));
  fputs("    return true;\n"
        "  } else {\n"
        "    loader->error_info.type = YAML_LOADER_ERROR_VALUE;\n", out);
  fprintf(out,
        "    const char typename[] = \"%s\";\n", type_descriptor->spelling);
  fputs("    loader->error_info.expected = malloc(sizeof(typename));\n"
        "    if (loader->error_info.expected == NULL) {\n"
        "      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\n"
        "      yaml_event_delete(cur);\n"
        "    } else {\n"
        "      loader->error_info.type = YAML_LOADER_ERROR_VALUE;\n"
        "      memcpy(loader->error_info.expected, typename, sizeof(typename));\n"
        "      loader->error_info.event = *cur;\n"
        "    }\n"
        "    return false;\n"
        "  }\n"
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
    if (type_descriptor->type.kind == CXType_Unexposed ||
        type_descriptor->flags.custom) continue;
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
  /* disabled because it requires a reference to the runtime in the generator */ \
  /*(void)&(constructor); // ensure constructor exists */\
  type_descriptor_t desc = {\
    .constructor_decl = "bool " #constructor,\
    .constructor_name_len = sizeof(#constructor),\
    .destructor_decl = NULL, .destructor_name_len = 0,\
    .spelling = #name};\
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

  char const **clang_args =
      malloc((argc - config.first_clang_param + 1) * sizeof(char*));
  clang_args[0] = "-std=c11";
  for (size_t i = 1; i <= argc - config.first_clang_param; ++i) {
    clang_args[i] = argv[i + config.first_clang_param - 1];
  }

  CXIndex index = clang_createIndex(0, 1);
  CXTranslationUnit unit =
      clang_parseTranslationUnit(index, config.input_file_path, clang_args,
                                 argc - config.first_clang_param + 1,
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
  memset(types_list.names.nodes[0]->followers, -1, 256 * sizeof(uint16_t));
  types_list.names.nodes[0]->type_index = -1;

  // known types
  KNOWN_TYPE(short, yaml_construct_short);
  KNOWN_TYPE(int, yaml_construct_int);
  KNOWN_TYPE(long, yaml_construct_long);
  KNOWN_TYPE(long long, yaml_construct_long_long);

  KNOWN_TYPE(unsigned char, yaml_construct_unsigned_char);
  KNOWN_TYPE(unsigned short, yaml_construct_unsigned_short);
  KNOWN_TYPE(unsigned int, yaml_construct_unsigned);
  KNOWN_TYPE(unsigned long, yaml_construct_unsigned_long);
  KNOWN_TYPE(unsigned long long, yaml_construct_unsigned_long_long);

  KNOWN_TYPE(float, yaml_construct_float);
  KNOWN_TYPE(double, yaml_construct_double);
  KNOWN_TYPE(long double, yaml_construct_long_double);

  KNOWN_TYPE(char, yaml_construct_char);
  KNOWN_TYPE(_Bool, yaml_construct_bool);

  type_info_t type_info = {.list = &types_list,
      .constructor_names = {.data = malloc(16 * sizeof(char*)),
          .count = 0, .capacity = 16},
      .destructor_names = {.data = malloc(16 * sizeof(char*)),
          .count = 0, .capacity = 16}};
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
  char const *const type_spelling =
      clang_getCString(clang_getTypeSpelling(root_type->type));
  const char *const space = strchr(type_spelling, ' ');


  FILE *const header_out = fopen(config.output_header_path, "w");
  if (header_out == NULL) {
    fprintf(stderr, "unable to open '%s' for writing.\n",
            config.output_header_path);
    return 1;
  }
  fprintf(header_out,
          "#include <yaml.h>\n"
          "#include <yaml_loader.h>\n"
          "#include <%s>\n", config.input_file_name);
  fputs("\n/* main functions for loading / deallocating the root type */\n\n",
        header_out);
  if (space == NULL) {
    fprintf(header_out,
            "bool " LOADER_PREFIX "%s(%s *value, yaml_loader_t *loader);\n"
            "void " DEALLOCATOR_PREFIX "%s(%s *value);\n",
            type_spelling, type_spelling, type_spelling, type_spelling);
  } else {
    fprintf(header_out,
            "bool " LOADER_PREFIX
                "%.*s_%s(%s *value, yaml_loader_t *loader);\n"
            "void " DEALLOCATOR_PREFIX "%.*s_%s(%s *value);\n",
            (int)(space - type_spelling), type_spelling, space + 1,
            type_spelling, (int)(space - type_spelling), type_spelling,
            space + 1, type_spelling);
  }
  fputs("\n/* low-level functions; "
        "only necessary when writing custom constructors */\n\n", header_out);
  if (!write_decls(&type_info, header_out)) return 1;
  fclose(header_out);

  FILE *const out_impl = fopen(config.output_impl_path, "w");
  if (out_impl == NULL) {
    fprintf(stderr, "Unable to open '%s' for writing.\n",
            config.output_impl_path);
    return 1;
  }
  fprintf(out_impl,
          "#include <yaml_constructor.h>\n"
          "#include <stdbool.h>\n"
          "#include <locale.h>\n"
          "#include <stdint.h>\n"
          "#include \"%s\"\n", config.output_header_name);

  write_static_decls(&types_list, out_impl);
  if (!write_impls(&types_list, out_impl)) return 1;

  if (space == NULL) {
    fprintf(out_impl, "bool " LOADER_PREFIX
                          "%s(%s *value, yaml_loader_t *loader) {\n",
            type_spelling, type_spelling);
  } else {
    fprintf(out_impl,
            "bool " LOADER_PREFIX
                "%.*s_%s(%s *value, yaml_loader_t *loader) {\n",
            (int)(space - type_spelling), type_spelling, space + 1,
            type_spelling);
  }
  fprintf(out_impl,
          "  char *old_locale = setlocale(LC_NUMERIC, NULL);\n"
          "  setlocale(LC_NUMERIC, \"C\");\n"
          "  yaml_event_t event;\n"
          "  if (yaml_parser_parse(loader->parser, &event) == 0) {\n"
          "    loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
          "    return false;\n"
          "  }\n"
          "  if (event.type == YAML_STREAM_START_EVENT) {\n"
          "    yaml_event_delete(&event);\n"
          "    if (yaml_parser_parse(loader->parser, &event) == 0) {\n"
          "      loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
          "      return false;\n"
          "    }\n"
          "  }\n"
          "  if (!yaml_constructor_check_event_type(loader, &event, "
          "YAML_DOCUMENT_START_EVENT))\n"
          "    return false;\n"
          "  yaml_event_delete(&event);\n"
          "  if (yaml_parser_parse(loader->parser, &event) == 0) {\n"
          "    loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
          "    return false;\n"
          "  }\n"
          "  bool ret = %.*s(value, loader, &event);\n"
          "  if (ret) {\n"
          "    yaml_event_delete(&event);\n"
          "    if (yaml_parser_parse(loader->parser, &event) == 0) {\n"
          "      loader->error_info.type = YAML_LOADER_ERROR_PARSER;\n"
          "      return false;\n"
          "    } else if (!yaml_constructor_check_event_type(loader, &event, "
          "YAML_DOCUMENT_END_EVENT))\n"
          "      return false;\n"
          "    yaml_event_delete(&event);\n"
          "  }\n"
          "  setlocale(LC_NUMERIC, old_locale);\n"
          "  return ret;\n"
          "}\n", (int)root_type->constructor_name_len,
          root_type->constructor_decl + sizeof(CONSTRUCTOR_PREAMBLE));
  char *const destructor_call =
      render_destructor_call(root_type, "value", true);
  if (space == NULL) {
    fprintf(out_impl,
            "void " DEALLOCATOR_PREFIX "%s(%s *value) {\n"
            "  %s\n"
            "}\n", type_spelling, type_spelling,
            destructor_call == NULL ? "" : destructor_call);
  } else {
    fprintf(out_impl,
            "void " DEALLOCATOR_PREFIX "%.*s_%s(%s *value) {\n"
            "  %s\n"
            "}\n", (int)(space - type_spelling), type_spelling, space + 1,
            type_spelling, destructor_call == NULL ? "" : destructor_call);
  }
  if (destructor_call != NULL) free(destructor_call);
  fclose(out_impl);

  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
}