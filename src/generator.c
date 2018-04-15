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
    (*process_t)(walker_t* const, CXCursor const cursor);

typedef struct {
  CXCursor parent, self;
  process_t enter, leave;
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

static process_result_t enter_struct(walker_t* walker,
                                   CXCursor cursor);
static process_result_t leave_struct(walker_t* walker,
                                   CXCursor cursor);
static process_result_t leave_list(walker_t* walker, CXCursor cursor);

static process_result_t enter_field(walker_t* walker,
                                  CXCursor cursor);
static process_result_t leave_field(walker_t* walker,
                                  CXCursor cursor);

static process_result_t enter_list_field(walker_t* walker,
                                         CXCursor cursor);
static process_result_t leave_list_field(walker_t* walker,
                                         CXCursor cursor);

/*
static process_result_t enter_typeref(walker_t* const walker,
                                      CXCursor const cursor);
static process_result_t leave_typeref(walker_t* const walker,
                                      CXCursor const cursor);*/

static inline dea_node_t* new_node() {
  dea_node_t* const val = malloc(sizeof(dea_node_t));
  memset(val->followers, -1, sizeof(val->followers));
  val->loader_implementation = NULL;
  return val;
}

static inline level_t struct_level(CXCursor parent) {
  const level_t ret = {parent, {}, &enter_struct, &leave_struct, false, NULL};
  return ret;
}

static inline level_t field_level(CXCursor parent) {
  dea_t* const dea = malloc(sizeof(dea_t));
  dea->count = 1;
  dea->nodes[0] = new_node();
  const level_t ret = {parent, {}, &enter_field, &leave_field, false, dea};
  return ret;
}

static inline level_t list_field_level(CXCursor parent) {
  list_info_t* const list_info = malloc(sizeof(list_info_t));
  list_info->seen_capacity = false;
  list_info->seen_count = false;
  list_info->data_type.kind = CXType_Unexposed;
  const level_t ret =
      {parent, {}, &enter_list_field, &leave_list_field, false, list_info};
  return ret;
}

/*
static inline level_t typeref_level(CXCursor parent) {
  const level_t ret = {parent, {}, &enter_typeref, &leave_typeref, false};
  return ret;
}*/

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

static char* get_annotation(CXCursor cursor) {
  const char* const comment =
      clang_getCString(clang_Cursor_getRawCommentText(cursor));
  // comment starts with either "//" or "/*" and therefore comment[2] always
  // exists.
  if (comment != NULL && comment[2] == '!') {
    const char* const start = comment + 3;
    const char* pos = start;
    while (*pos != ' ' && *pos != '\r' && *pos != '\n' && *pos != '\0') pos++;
    char* const ret = malloc(pos - start + 1);
    memcpy(ret, comment + 3, pos - start);
    ret[pos - start] = '\0';
    return ret;
  } else {
    return NULL;
  }
}

static const char* raw_name(CXType type) {
  const char* full_name = clang_getCString(clang_getTypeSpelling(type));
  const char* space = strchr(full_name, ' ');
  if (space != NULL) return space + 1;
  else return full_name;
}

static process_result_t enter_field(walker_t* const walker,
                                    CXCursor const cursor) {
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

  dea_t* dea = (dea_t*) walker->levels[walker->cur_level].data;
  dea_node_t* cur_node = dea->nodes[0];
  
  for (char const* cur_char = name; *cur_char != 0; ++cur_char) {
    int node_id = cur_node->followers[(int)(*cur_char)];
    if (node_id == -1) {
      node_id = (int) dea->count++;
      if (node_id == MAX_NODES) {
        fputs("too many nodes in DEA!\n", stderr);
        return error;
      }
      cur_node->followers[(int)(*cur_char)] = node_id;
      dea->nodes[node_id] = new_node();
    }
    cur_node = dea->nodes[node_id];
  }
  
  char* annotation = get_annotation(cursor);
  process_result_t ret;
  
  if (annotation != NULL) {
    if (!strcmp(annotation, "string")) {
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
      fprintf(stderr, "Unknown annotation: '%s'", annotation);
      ret = error;
    }
    free(annotation);
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
            new_deserialization(name, raw_name(t));
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

static process_result_t leave_field(walker_t* const walker,
                                    CXCursor const cursor) {
  (void)walker; (void)cursor;
  // nothing to be done right now
  return okay;
}

static process_result_t enter_list_field(walker_t* const walker,
                                         CXCursor const cursor) {
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
static process_result_t leave_list_field(walker_t* const walker,
                                         CXCursor const cursor) {
  (void)walker; (void)cursor;
  // nothing to do here right now
  return okay;
}

static process_result_t enter_struct(walker_t* const walker,
                                     CXCursor const cursor) {
  const enum CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind != CXCursor_StructDecl) {
    fprintf(stderr, "Unexpected top-level item: %s",
            clang_getCString(clang_getCursorKindSpelling(kind)));
    return error;
  }
  
  char* annotation = get_annotation(cursor);
  process_result_t ret;
  if (annotation != NULL) {
    if (!strcmp(annotation, "list")) {
      level(walker)->leave = &leave_list;
      push_level(walker, list_field_level(cursor));
      ret = okay;
    } else {
      fprintf(stderr, "Unknown annotation: %s", annotation);
      ret = error;
    }
    free(annotation);
  } else {
    push_level(walker, field_level(cursor));
    ret = okay;
  }
  CXType t = clang_getCanonicalType(clang_getCursorType(cursor));
  const char* name = raw_name(t);
  if (!strcmp(name, walker->rootName)) {
    walker->rootType = t;
  }
  
  fprintf(walker->loader_out,
          "static char* construct_%s(%s * const value, "
          "yaml_parser_t* const parser, yaml_event_t* cur) {\n",
          name, clang_getCString(clang_getTypeSpelling(t)));
  return ret;
}

static process_result_t leave_struct(walker_t* const walker,
                                     CXCursor const cursor) {
  (void)cursor;
  dea_t* dea = (dea_t*) walker->levels[walker->cur_level + 1].data;
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
  fputs("  };\n"
        "  if (cur->type != YAML_MAPPING_START_EVENT) {\n"
        "    return wrong_event_error(YAML_MAPPING_START_EVENT, cur->type);\n"
        "  }"
        "  yaml_event_t key;\n"
        "  yaml_parser_parse(parser, &key);\n"
        "  char* ret = NULL;\n"
        "  while(key.type != YAML_MAPPING_END_EVENT) {\n"
        "    if (key.type != YAML_SCALAR_EVENT) {\n"
        "      ret = wrong_event_error(YAML_SCALAR_EVENT, key.type);\n"
        "      break;\n"
        "    }\n"
        "    int8_t result = walk(table, "
        "(const char*)key.data.scalar.value);\n"
        "    yaml_event_t event;\n"
        "    yaml_parser_parse(parser, &event);\n"
        "    switch(result) {\n", walker->loader_out);
  for (size_t i = 0; i < dea->count; i++) {
    if (dea->nodes[i]->loader_implementation != NULL) {
      fprintf(walker->loader_out, "      case %zu:\n        ", i);
      fputs(dea->nodes[i]->loader_implementation, walker->loader_out);
      free(dea->nodes[i]->loader_implementation);
      fputs("        break;\n", walker->loader_out);
    }
    free(dea->nodes[i]);
  }
  fputs("      default: {\n"
        "          size_t escaped_len;\n"
        "          char* escaped = escape((const char*)key.data.scalar.value, "
        "&escaped_len);\n"
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
            complete_type, complete_type, raw_name(list_info->data_type));
  }
  free(list_info);
  level(walker)->leave = &leave_struct;
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
    walker->levels[0] = struct_level(parent);
  }
  if (level(walker)->entered) {
    do {
      if ((level(walker)->leave)(walker, level(walker)->self) != okay) {
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
  if ((*level(walker)->enter)(walker, cursor) != okay) {
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
      if (i == argc - 1) {
        fprintf(stderr, "switch %s is missing value!", argv[i]);
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
          "#include \"%s\"\n", output_header_name);
  
  clang_visitChildren(cursor, &visitor, &walker);
  if (!walker.got_errors) {
    while (walker.cur_level >= 0) {
      (*walker.levels[walker.cur_level].leave)(&walker, level(&walker)->self);
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
  }
  
  fclose(walker.loader_out);
  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
}
