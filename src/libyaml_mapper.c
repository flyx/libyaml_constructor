#include <memory.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <clang-c/Index.h>

struct walker_struct;
typedef struct walker_struct walker_t;

typedef enum {okay, error} process_result_t;

typedef process_result_t
    (*exit_process_t)(walker_t* const, CXCursor const);

typedef process_result_t
    (*enter_process_t)(walker_t* const, CXCursor const cursor, exit_process_t*);

typedef struct {
  CXCursor parent, self;
  enter_process_t enter;
  exit_process_t leave;
  bool entered;
  void* data;
} level_t;

struct walker_struct {
  level_t levels[10];
  int cur_level;
  FILE* loader_out;
  bool got_errors;
  const char* rootName;
  CXType rootType;
};

typedef struct dea_node_struct dea_node_t;

struct dea_node_struct {
  int followers[256];
  char* loader_implementation;
  const char* loader_item_name;
};

#define MAX_NODES 2048

typedef struct {
  dea_node_t* nodes[MAX_NODES];
  size_t count;
} dea_t;

typedef struct {
  bool seen_count, seen_capacity;
  CXType data_type;
} list_info_t;

typedef struct {
  char *name, *param;
} annotation_t;

static process_result_t enter_toplevel(walker_t*, CXCursor, exit_process_t*);
static process_result_t enter_typedef(walker_t*, CXCursor, exit_process_t*);
static process_result_t leave_struct(walker_t*, CXCursor);
static process_result_t leave_list(walker_t*, CXCursor);
static process_result_t leave_enum(walker_t*, CXCursor);

static process_result_t enter_struct_item(walker_t*, CXCursor, exit_process_t*);
static process_result_t enter_list_item(walker_t*, CXCursor, exit_process_t*);
static process_result_t enter_enum_item(walker_t*, CXCursor, exit_process_t*);

static inline dea_node_t* new_node(const char* name) {
  dea_node_t* const val = malloc(sizeof(dea_node_t));
  memset(val->followers, -1, sizeof(val->followers));
  val->loader_implementation = NULL;
  val->loader_item_name = name;
  return val;
}

static inline level_t top_level(CXCursor parent) {
  const level_t ret = {parent, {}, &enter_toplevel, NULL, false, NULL};
  return ret;
}

static inline level_t typedef_level(CXCursor parent, const char* name) {
  const level_t ret = {parent, {}, &enter_typedef, NULL, false, (void*)name};
  return ret;
}

static inline level_t struct_level(CXCursor parent) {
  dea_t* const dea = malloc(sizeof(dea_t));
  dea->count = 1;
  dea->nodes[0] = new_node(NULL);
  const level_t ret = {parent, {}, &enter_struct_item, NULL, false, dea};
  return ret;
}

static inline level_t enum_level(CXCursor parent) {
  dea_t* const dea = malloc(sizeof(dea_t));
  dea->count = 1;
  dea->nodes[0] = new_node(NULL);
  const level_t ret = {parent, {}, &enter_enum_item, NULL, false, dea};
  return ret;
}

static inline level_t list_level(CXCursor parent) {
  list_info_t* const list_info = malloc(sizeof(list_info_t));
  list_info->seen_capacity = false;
  list_info->seen_count = false;
  list_info->data_type.kind = CXType_Unexposed;
  const level_t ret =
      {parent, {}, &enter_list_item, NULL, false, list_info};
  return ret;
}

static inline level_t* level(walker_t* const walker) {
  return &walker->levels[walker->cur_level];
}

static inline void push_level(walker_t* const walker, level_t l) {
  walker->levels[++walker->cur_level] = l;
}

static char* new_deserialization(const char* const field, const char* type) {
  static const char template[] =
      "ret = construct_%s(&value->%s, parser, &event);\n";
  const size_t tmpl_len = sizeof(template) - 1;

  const size_t res_len = tmpl_len + strlen(field) + strlen(type) - 3;
  char* ret = malloc(res_len);
  sprintf(ret, template, type, field);
  return ret;
}

static bool get_annotation(CXCursor cursor, bool has_param,
                           annotation_t* annotation) {
  const char* const comment =
      clang_getCString(clang_Cursor_getRawCommentText(cursor));
  // comment starts with either "//" or "/*" and therefore comment[2] always
  // exists.
  if (comment != NULL && comment[2] == '!') {
    const char* const start = comment + 3;
    const char* pos = start;
    while (*pos != ' ' && *pos != '\r' && *pos != '\n' && *pos != '\0') pos++;
    const size_t name_len = pos - start;
    if (has_param) {
      while (*pos == ' ' || *pos == '\t') pos++;
      if (*pos == '\r' || *pos == '\n' || *pos == '\0') {
        fprintf(stderr, "annotation %.*s is missing its parameter!\n",
                (int)name_len, start);
        return false;
      }
      const char* param_start = pos;
      do {pos++;} while (*pos != ' ' && *pos != '\r' && *pos != '\n' &&
                        *pos != '\0');
      const size_t param_len = pos - param_start;
      char* const ret = malloc(name_len + param_len + 2);
      memcpy(ret, start, name_len);
      ret[name_len] = '\0';
      memcpy(ret + name_len + 1, param_start, param_len);
      ret[name_len + param_len + 1] = '\0';
      annotation->name = ret;
      annotation->param = ret + name_len + 1;
    } else {
      char *const ret = malloc(pos - start + 1);
      memcpy(ret, comment + 3, pos - start);
      ret[pos - start] = '\0';
      annotation->name = ret;
      annotation->param = NULL;
    }
    return true;
  } else {
    annotation->name = NULL;
    annotation->param = NULL;
    return true;
  }
}

static void free_annotation(annotation_t annotation) {
  free(annotation.name);
}

static const char* raw_name(CXString value) {
  const char* full_name = clang_getCString(value);
  const char* space = strchr(full_name, ' ');
  if (space != NULL) return space + 1;
  else return full_name;
}

static dea_node_t* include_name(dea_t* const dea, const char* name) {
  dea_node_t* cur_node = dea->nodes[0];
  for (char const* cur_char = name; *cur_char != '\0'; ++cur_char) {
    int node_id = cur_node->followers[(int)(*cur_char)];
    if (node_id == -1) {
      node_id = (int) dea->count++;
      if (node_id == MAX_NODES) {
        fputs("too many nodes in DEA!\n", stderr);
        return NULL;
      }
      cur_node->followers[(int)(*cur_char)] = node_id;
      dea->nodes[node_id] = new_node(name);
    }
    cur_node = dea->nodes[node_id];
  }
  return cur_node;
}

static process_result_t enter_struct_item(walker_t* const walker,
                                          CXCursor const cursor,
                                          exit_process_t* const exit_process) {
  *exit_process = NULL;
  const enum CXCursorKind kind = clang_getCursorKind(cursor);
  switch (kind) {
    case CXCursor_StructDecl:
      fprintf(stderr, "nested structs not allowed!\n");
      return error;
    case CXCursor_FieldDecl: break;
    default:
      fprintf(stderr, "Unexpected item in struct (expected field): %s",
              clang_getCString(clang_getCursorKindSpelling(kind)));
      return error;
  }
  const char* const name = clang_getCString(clang_getCursorSpelling(cursor));
  const CXType t = clang_getCanonicalType(clang_getCursorType(cursor));

  dea_t* dea = (dea_t*) level(walker)->data;
  dea_node_t* cur_node = include_name(dea, name);
  if (!cur_node) return error;
  
  annotation_t annotation;
  bool success = get_annotation(cursor, false, &annotation);
  if (!success) return error;
  process_result_t ret;
  
  if (annotation.name != NULL) {
    if (!strcmp(annotation.name, "string")) {
      if (t.kind != CXType_Pointer) {
        fprintf(stderr, "'!string' must be applied on a char pointer "
                        "(found on a '%s')!\n",
                clang_getCString(clang_getTypeSpelling(t)));
        ret = error;
      } else {
        const CXType pointee = clang_getPointeeType(t);
        if (pointee.kind != CXType_Char_S) {
            fprintf(stderr, "'!string' must be applied on a char pointer "
                            "(found on a '%s')!\n",
                    clang_getCString(clang_getTypeSpelling(t)));
        }
        cur_node->loader_implementation = new_deserialization(name, "string");
        ret = okay;
      }
    } else {
      fprintf(stderr, "Unknown annotation: '%s'", annotation.name);
      ret = error;
    }
    free_annotation(annotation);
  } else {
    switch (t.kind) {
      case CXType_Int:
        cur_node->loader_implementation = new_deserialization(name, "int");
        ret = okay;
        break;
      case CXType_Char_S:
        cur_node->loader_implementation = new_deserialization(name, "char");
        ret = okay;
        break;
      case CXType_Record:
        // TODO: ensure a loader for this record exists
        cur_node->loader_implementation =
            new_deserialization(name, raw_name(clang_getTypeSpelling(t)));
        ret = okay;
        break;
      case CXType_Enum:
        // TODO: ensure a loader for this enum exists
        cur_node->loader_implementation =
            new_deserialization(name, raw_name(clang_getTypeSpelling(t)));
        ret = okay;
        break;
      default:
        fprintf(stderr, "Target type not implemented: %s",
                clang_getCString(clang_getTypeSpelling(t)));
        ret = error;
    }
  }
  return ret;
}

static process_result_t enter_enum_item(walker_t* const walker,
                                        CXCursor const cursor,
                                        exit_process_t* exit_process) {
  *exit_process = NULL;
  const enum CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind != CXCursor_EnumConstantDecl) {
      fprintf(stderr,
              "Unexpected item in enum type (expected enum constant): %s",
              clang_getCString(clang_getCursorKindSpelling(kind)));
      return error;
  }
  const char* const name = clang_getCString(clang_getCursorSpelling(cursor));

  annotation_t annotation;
  bool success = get_annotation(cursor, true, &annotation);
  if (!success) return error;
  const char* representation = name;

  if (annotation.name != NULL) {
    if (!strcmp(annotation.name, "repr")) {
      representation = annotation.param;
    } else {
      fprintf(stderr, "Unknown annotation: '%s'", annotation.name);
      free_annotation(annotation);
      return error;
    }
  }

  dea_t* dea = (dea_t*) level(walker)->data;
  dea_node_t* cur_node = include_name(dea, representation);
  if (annotation.name != NULL) free_annotation(annotation);
  if (!cur_node) return error;

  static const char template[] = "      *value = %s;\n";
  const size_t impl_len = sizeof(template) + strlen(name) - 1;
  char* const impl = malloc(impl_len);
  sprintf(impl, template, name);
  cur_node->loader_implementation = impl;
  return okay;
}

static process_result_t enter_list_item(walker_t* const walker,
                                        CXCursor const cursor,
                                        exit_process_t* exit_process) {
  *exit_process = NULL;
  list_info_t* list_info = (list_info_t*) level(walker)->data;
  const enum CXCursorKind kind = clang_getCursorKind(cursor);
  switch (kind) {
    case CXCursor_StructDecl:
      fprintf(stderr, "nested structs not allowed!\n");
      return error;
    case CXCursor_FieldDecl: break;
    default:
      fprintf(stderr, "Unexpected item in struct (expected field): %s",
              clang_getCString(clang_getCursorKindSpelling(kind)));
      return error;
  }
  const char* const name = clang_getCString(clang_getCursorSpelling(cursor));
  CXType t = clang_getCanonicalType(clang_getCursorType(cursor));

  if (!strcmp(name, "data")) {
    if (t.kind != CXType_Pointer) {
      fputs("data field of list must be a pointer!\n", stderr);
      return error;
    }
    const CXType pointee = clang_getPointeeType(t);
    if (pointee.kind == CXType_Pointer) {
      fputs("pointer to pointer not supported as list!\n", stderr);
      return error;
    }
    list_info->data_type = pointee;
  } else if (!strcmp(name, "count")) {
    if (t.kind != CXType_UChar && t.kind != CXType_UShort &&
        t.kind != CXType_UInt && t.kind != CXType_ULong &&
        t.kind != CXType_ULongLong) {
      fprintf(stderr, "\"count\" field must be an unsigned type (found %i)!\n",
          t.kind);
      return error;
    }
    list_info->seen_count = true;
  } else if (!strcmp(name, "capacity")) {
    if (t.kind != CXType_UChar && t.kind != CXType_UShort &&
        t.kind != CXType_UInt && t.kind != CXType_ULong &&
        t.kind != CXType_ULongLong) {
      fputs("\"capacity\" field must be an unsigned type!\n", stderr);
      return error;
    }
    list_info->seen_capacity = true;
  } else {
    fprintf(stderr, "illegal field \"%s\" for list!\n", name);
    return error;
  }
  return okay;
}

static process_result_t enter_struct_decl(walker_t* const walker,
                                          CXCursor const cursor,
                                          exit_process_t* exit_process) {
  annotation_t annotation;
  bool success = get_annotation(cursor, false, &annotation);
  if (!success) return error;
  process_result_t ret;
  if (annotation.name != NULL) {
    if (!strcmp(annotation.name, "list")) {
      *exit_process = &leave_list;
      push_level(walker, list_level(cursor));
      ret = okay;
    } else {
      fprintf(stderr, "Unknown annotation: %s", annotation.name);
      ret = error;
    }
    free_annotation(annotation);
  } else {
    *exit_process = &leave_struct;
    push_level(walker, struct_level(cursor));
    ret = okay;
  }
  return ret;
}

static process_result_t enter_enum_decl(walker_t* const walker,
                                        CXCursor const cursor,
                                        exit_process_t* exit_process) {
  push_level(walker, enum_level(cursor));
  *exit_process = &leave_enum;
  return okay;
}

static process_result_t enter_def_level(walker_t* const walker,
                                        CXCursor const cursor,
                                        exit_process_t* exit_process,
                                        const char* typedef_name) {
  CXType t = clang_getCursorType(cursor);
  const char* simple_name = raw_name(clang_getTypeSpelling(t));
  if (!strcmp(simple_name, walker->rootName)) {
    walker->rootType = t;
  }

  process_result_t ret;
  const char* actual_name = clang_getCString(clang_getCursorSpelling(cursor));
  if (actual_name[0] == '\0') {
    // ignore anonymous struct/enum at top level
    // (probably a typedef, will be handled there)
    *exit_process = NULL;
    return okay;
  }

  const enum CXCursorKind kind = clang_getCursorKind(cursor);
  switch(kind) {
    case CXCursor_StructDecl:
      ret = enter_struct_decl(walker, cursor, exit_process);
      break;
    case CXCursor_EnumDecl:
      ret = enter_enum_decl(walker, cursor, exit_process);
      break;
    case CXCursor_TypedefDecl:
      push_level(walker, typedef_level(cursor, actual_name));
      *exit_process = NULL;
      return okay;
    default:
      fprintf(stderr, "Unexpected top-level item: %s\n",
              clang_getCString(clang_getCursorKindSpelling(kind)));
      return error;
  }

  fprintf(walker->loader_out,
          "static char* construct_%s(%s * const value, "
          "yaml_parser_t* const parser, yaml_event_t* cur) {\n",
          typedef_name ? typedef_name : simple_name,
          typedef_name ? typedef_name :
              clang_getCString(clang_getTypeSpelling(t)));
  return ret;
}

static process_result_t enter_toplevel(walker_t* const walker,
                                       CXCursor const cursor,
                                       exit_process_t* exit_process) {
  return enter_def_level(walker, cursor, exit_process, NULL);
}

static process_result_t enter_typedef(walker_t* const walker,
                                      CXCursor const cursor,
                                      exit_process_t* exit_process) {
  return enter_def_level(walker, cursor, exit_process,
                         (const char*)level(walker)->data);
}

static void put_control_table(walker_t* const walker, const dea_t* const dea) {
  fputs("  static const int8_t table[][256] = {\n", walker->loader_out);
  for(int i = 0; i < dea->count; ++i) {
    fputs("      {", walker->loader_out);
    for(int j = 0; j < 256; ++j) {
      if (j > 0) fputs(", ", walker->loader_out);
      fprintf(walker->loader_out, "%d", dea->nodes[i]->followers[j]);
    }
    if (i < dea->count - 1) fputs("},\n", walker->loader_out);
    else fputs("}\n", walker->loader_out);
  }
  fputs("  };\n", walker->loader_out);
}

static void process_struct_nodes(walker_t* const walker,
                                 const dea_t* const dea) {
  size_t index = 0;
  for (size_t i = 0; i < dea->count; i++) {
    if (dea->nodes[i]->loader_implementation != NULL) {
      fprintf(walker->loader_out,
              "      case %zu:\n"
              "        if (found[%zu]) {\n"
              "          size_t escaped_len;\n"
              "          char* escaped = escape(name, &escaped_len);\n"
              "          ret = malloc(16 + escaped_len);\n"
              "          sprintf(ret, \"duplicate key: %%s\", escaped);\n"
              "        } else {\n"
              "          found[%zu] = true;\n"
              "          ", i, index, index);
      index++;
      fputs(dea->nodes[i]->loader_implementation, walker->loader_out);
      free(dea->nodes[i]->loader_implementation);
      fputs("          }\n"
            "          break;\n", walker->loader_out);
    }
    free(dea->nodes[i]);
  }
}

static process_result_t leave_struct(walker_t* const walker,
                                     CXCursor const cursor) {
  (void)cursor;
  dea_t* dea = (dea_t*) walker->levels[walker->cur_level + 1].data;
  put_control_table(walker, dea);
  fputs("  if (cur->type != YAML_MAPPING_START_EVENT) {\n"
        "    return wrong_event_error(YAML_MAPPING_START_EVENT, cur->type);\n"
        "  }\n"
        "  yaml_event_t key;\n"
        "  yaml_parser_parse(parser, &key);\n"
        "  char* ret = NULL;\n"
        "  bool found[] = {", walker->loader_out);
  bool first = true;
  for (size_t i = 0; i < dea->count; i++) {
    if (dea->nodes[i]->loader_implementation != NULL) {
      if (first) first = false;
      else fputs(", ", walker->loader_out);
      fputs("false", walker->loader_out);
    }
  }
  fputs("};\n"
        "  static const char* names[] = {", walker->loader_out);
  first = true;
  for (size_t i = 0; i < dea->count; i++) {
    if (dea->nodes[i]->loader_implementation != NULL) {
      if (first) first = false;
      else fputs(", ", walker->loader_out);
      fprintf(walker->loader_out, "\"%s\"", dea->nodes[i]->loader_item_name);
    }
  }
  fputs("};\n"
        "  while(key.type != YAML_MAPPING_END_EVENT) {\n"
        "    if (key.type != YAML_SCALAR_EVENT) {\n"
        "      ret = wrong_event_error(YAML_SCALAR_EVENT, key.type);\n"
        "      break;\n"
        "    }\n"
        "    int8_t result = walk(table, "
        "(const char*)key.data.scalar.value);\n"
        "    yaml_event_t event;\n"
        "    yaml_parser_parse(parser, &event);\n"
        "    const char* const name = (const char*)key.data.scalar.value;\n"
        "    switch(result) {\n", walker->loader_out);
  process_struct_nodes(walker, dea);
  fputs("      default: {\n"
        "          size_t escaped_len;\n"
        "          char* escaped = escape(name, &escaped_len);\n"
        "          ret = malloc(16 + escaped_len);\n"
        "          sprintf(ret, \"unknown field: %s\", escaped);\n"
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
        "  for (size_t i = 0; i < sizeof(found); i++) {\n"
        "    if (!found[i]) {\n"
        "      const size_t name_len = strlen(names[i]);\n"
        "      ret = malloc(17 + name_len);\n"
        "      sprintf(ret, \"missing value for field \\\"%s\\\"\", names[i]);\n"
        "      break;\n"
        "    }\n"
        "  }\n"
        "  return ret;\n"
        "}\n\n", walker->loader_out);
  free(dea);
  return okay;
}

static void process_enum_nodes(walker_t* const walker,
                                 const dea_t* const dea) {
  for (size_t i = 0; i < dea->count; i++) {
    if (dea->nodes[i]->loader_implementation != NULL) {
      fprintf(walker->loader_out,
              "      case %zu:\n"
              "        ", i);
      fputs(dea->nodes[i]->loader_implementation, walker->loader_out);
      free(dea->nodes[i]->loader_implementation);
      fputs("          break;\n", walker->loader_out);
    }
    free(dea->nodes[i]);
  }
}

static process_result_t leave_enum(walker_t* const walker,
                                   CXCursor const cursor) {
  (void)cursor;
  dea_t* dea = (dea_t*) walker->levels[walker->cur_level + 1].data;
  put_control_table(walker, dea);
  fputs("  if (cur->type != YAML_SCALAR_EVENT) {\n"
        "    return wrong_event_error(YAML_SCALAR_EVENT, cur->type);\n"
        "  }\n"
        "  int8_t result = walk(table, "
        "(const char*)cur->data.scalar.value);\n"
        "  char* ret = NULL;\n"
        "  switch(result) {\n", walker->loader_out);
  process_enum_nodes(walker, dea);
  fputs("    default: {\n"
        "      size_t escaped_len;\n"
        "      char* escaped = escape((const char*)cur->data.scalar.value, "
        "&escaped_len);\n"
        "      ret = malloc(21 + escaped_len);\n"
        "      sprintf(ret, \"unknown enum value: %s\", escaped);\n"
        "      free(escaped);\n"
        "    }\n"
        "  }\n"
        "  return ret;\n"
        "}\n\n", walker->loader_out);
  free(dea);
  return okay;
}

static process_result_t leave_list(walker_t* const walker,
                                   CXCursor const cursor) {
  (void)cursor;
  list_info_t* list_info =
      (list_info_t*) walker->levels[walker->cur_level + 1].data;
  process_result_t ret = okay;
  if (list_info->data_type.kind == CXType_Unexposed) {
    fputs("data field for list missing!\n", stderr);
    ret = error;
  }
  if (!list_info->seen_count) {
    fputs("count field for list missing!\n", stderr);
    ret = error;
  }
  if (!list_info->seen_capacity) {
    fputs("capacity field for list missing!\n", stderr);
    ret = error;
  }
  if (ret == okay) {
    const char* complete_type =
        clang_getCString(clang_getTypeSpelling(list_info->data_type));
    fprintf(walker->loader_out,
            "  if (cur->type != YAML_SEQUENCE_START_EVENT) {\n"
            "    char* buffer = malloc(100);\n"
            "    sprintf(buffer, \"expected SEQUENCE_START, got %%s!\", "
                    "event_spelling(cur->type));\n"
            "    return buffer;\n"
            "  }\n"
            "  value->data = malloc(16 * sizeof(%s));\n"
            "  value->count = 0;\n"
            "  value->capacity = 16;\n"
            "  yaml_event_t event;\n"
            "  yaml_parser_parse(parser, &event);\n"
            "  while (event.type != YAML_SEQUENCE_END_EVENT) {\n"
            "    %s* item;\n"
            "    APPEND(value, item);\n"
            "    char* ret = construct_%s(item, parser, &event);\n"
            "    yaml_event_delete(&event);\n"
            "    if (ret) {\n"
            "      free(value->data);\n"
            "      return ret;\n"
            "    }\n"
            "    yaml_parser_parse(parser, &event);\n"
            "  }\n"
            "  yaml_event_delete(&event);\n"
            "  return NULL;\n"
            "}\n",
            complete_type, complete_type,
            raw_name(clang_getTypeSpelling(list_info->data_type)));
  }
  free(list_info);
  return ret;
}

enum CXChildVisitResult visitor(CXCursor cursor, CXCursor parent,
                                CXClientData client_data) {
  if (!clang_Location_isFromMainFile(clang_getCursorLocation(cursor))) {
    return CXChildVisit_Continue;
  }
  walker_t* walker = (walker_t*) client_data;
  if (walker->cur_level == -1) {
    walker->cur_level = 0;
    walker->levels[0] = top_level(parent);
  }
  if (level(walker)->entered) {
    do {
      if (level(walker)->leave &&
          (level(walker)->leave)(walker, level(walker)->self) != okay) {
        walker->got_errors = true;
        return CXChildVisit_Break;
      }
      if (clang_equalCursors(level(walker)->parent, parent)) {
        break;
      }
      --walker->cur_level;
    } while (walker->cur_level >= 0);
  }
  
  level(walker)->entered = true;
  level(walker)->self = cursor;
  if ((*level(walker)->enter)(walker, cursor, &level(walker)->leave) != okay) {
    walker->got_errors = true;
    return CXChildVisit_Break;
  }
  
  return level(walker)->entered ? CXChildVisit_Continue : CXChildVisit_Recurse;
}

void usage(const char* executable) {
  fprintf(stdout, "Usage: %s [options] file\n", executable);
  fputs("  options:\n"
        "    -o directory       writes output files to $directory (default: .)\n"
        "    -r name            expects the root type to be named $name.\n"
        "                       default: \"root\"\n"
        "    -n name            names output files $name.h and $name.c .\n"
        "                       default: $file without extension.\n", stdout);
}

const char* last_index(const char* string, char c) {
  char* res = NULL;
  char* next = strchr(string, c);
  while (next != NULL) {
    res = next;
    next = strchr(res + 1, c);
  }
  return res;
}

int main(const int argc, const char* argv[]) {
  // command line arguments
  const char* target_dir = NULL;
  const char* root_name = NULL;
  const char* output_name = NULL;
  const char* input_file = NULL;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (i == argc - 1 && argv[i][1] != 'h') {
        fprintf(stderr, "switch %s is missing value!\n", argv[i]);
        usage(argv[0]);
        return 1;
      }

      switch (argv[i][1]) {
        case 'o':
          if (target_dir != NULL) {
            fputs("duplicate -o switch!\n", stderr);
            usage(argv[0]);
            return 1;
          } else {
            target_dir = argv[++i];
          }
          break;
        case 'r':
          if (root_name != NULL) {
            fputs("duplicate -r switch!\n", stderr);
            usage(argv[0]);
            return 1;
          } else {
            root_name = argv[++i];
          }
          break;
        case 'n':
          if (output_name != NULL) {
            fputs("duplicate -n switch!\n", stderr);
            usage(argv[0]);
            return 1;
          } else {
            output_name = argv[++i];
          }
          break;
        case 'h':
          usage(argv[0]);
          return 0;
        default:
          fprintf(stderr, "unknown switch: '%s'\n", argv[i]);
          usage(argv[0]);
          return 1;
      }
    } else if (input_file != NULL) {
      fprintf(stderr, "unexpected parameter: '%s'", argv[i]);
      usage(argv[0]);
      return 1;
    } else {
      input_file = argv[i];
    }
  }
  if (target_dir == NULL) target_dir = ".";
  if (root_name == NULL) root_name = "root";
  if (input_file == NULL) {
    fputs("missing input file\n", stderr);
    usage(argv[0]);
    return 1;
  }
  if (output_name == NULL) {
    const char* dot = last_index(input_file, '.');
    const char* slash = last_index(input_file, '/');
    const char* start = slash == NULL ? input_file : slash + 1;
    size_t len = (dot == NULL || dot < slash) ? strlen(start) : dot - start;
    char* dest = malloc(len + 9);
    memcpy(dest, start, len);
    strcpy(dest + len, "_loading");
    output_name = dest;
  }

  size_t target_dir_len = strlen(target_dir);
  const size_t output_name_len = strlen(output_name);
  const size_t path_length = target_dir_len + output_name_len + 3;
  char* output_header_path = malloc(path_length);
  char* output_impl_path = malloc(path_length);
  memcpy(output_header_path, target_dir, target_dir_len);
  memcpy(output_impl_path, target_dir, target_dir_len);
  if (target_dir[target_dir_len - 1] != '/') {
    output_header_path[target_dir_len] = '/';
    output_impl_path[target_dir_len] = '/';
    ++target_dir_len;
  }
  memcpy(output_header_path + target_dir_len, output_name, output_name_len);
  strcpy(output_header_path + target_dir_len + output_name_len, ".h");
  memcpy(output_impl_path + target_dir_len, output_name, output_name_len);
  strcpy(output_impl_path + target_dir_len + output_name_len, ".c");
  const char* input_file_name = last_index(input_file, '/');
  if (input_file_name == NULL) input_file_name = input_file;
  else ++input_file_name;
  const char* output_header_name = output_header_path + target_dir_len;
  
  CXIndex index = clang_createIndex(0, 0);
  CXTranslationUnit unit = clang_parseTranslationUnit(index, input_file, NULL,
                                                      0, NULL, 0,
                                                      CXTranslationUnit_None);
  if (unit == NULL) {
    fprintf(stderr, "Unable to parse '%s'.\n", input_file);
    return 1;
  }
  
  CXCursor cursor = clang_getTranslationUnitCursor(unit);
  walker_t walker = {{}, -1, fopen(output_impl_path, "w"),
                     false, root_name, {CXType_Unexposed, NULL}};
  if (walker.loader_out == NULL) {
    fprintf(stderr, "Unable to open '%s' for writing.\n", output_impl_path);
    return 1;
  }
  fprintf(walker.loader_out,
          "#include <loader_common.h>\n"
          "#include <stdbool.h>\n"
          "#include \"%s\"\n", output_header_name);
  
  clang_visitChildren(cursor, &visitor, &walker);
  if (walker.got_errors) {
    return 1;
  }
  while (walker.cur_level >= 0) {
    exit_process_t exit_process = level(&walker)->leave;
    if (exit_process) (*exit_process)(&walker, level(&walker)->self);
    --walker.cur_level;
  }

  if (walker.rootType.kind == CXType_Unexposed)
    fprintf(stderr, "did not find root type '%s'!", walker.rootName);
  else {
    const char* type_spelling =
        clang_getCString(clang_getTypeSpelling(walker.rootType));
    fprintf(walker.loader_out,
    "char* load_one(%s* value, yaml_parser_t* parser) {\n"
    "  yaml_event_t event;\n"
    "  yaml_parser_parse(parser, &event);\n"
    "  if (event.type == YAML_STREAM_START_EVENT) {\n"
    "    yaml_event_delete(&event);\n"
    "    yaml_parser_parse(parser, &event);\n"
    "  }\n"
    "  if (event.type != YAML_DOCUMENT_START_EVENT) {\n"
    "    yaml_event_delete(&event);\n"
    "    return wrong_event_error(YAML_DOCUMENT_START_EVENT, event.type);\n"
    "  }\n"
    "  yaml_event_delete(&event);\n"
    "  yaml_parser_parse(parser, &event);\n"
    "  char* ret = construct_%s(value, parser, &event);\n"
    "  yaml_event_delete(&event);\n"
    "  yaml_parser_parse(parser, &event); // assume document end\n"
    "  yaml_event_delete(&event);\n"
    "  return ret;\n"
    "}\n", type_spelling, walker.rootName);
    FILE* header_out = fopen(output_header_path, "w");
    if (header_out == NULL) {
      fprintf(stderr, "unable to open '%s' for writing.\n",
              output_header_path);
      return 1;
    }
    fprintf(header_out,
            "#include <yaml.h>\n"
            "#include <%s>\n"
            "char* load_one(%s* value, yaml_parser_t* parser);\n",
            input_file_name, type_spelling);
    fclose(header_out);
  }
  
  fclose(walker.loader_out);
  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
}
