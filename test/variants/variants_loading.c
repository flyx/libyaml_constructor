#include <loader_common.h>
#include <stdbool.h>
#include "variants_loading.h"
static char* construct_enum__value_type(enum value_type* const value, yaml_parser_t* const parser, yaml_event_t* cur);
static void delete_enum__value_type(enum value_type* const value);
static bool convert_to_enum__value_type(const char* const value, enum value_type* const result);
static char* construct_struct__field(struct field* const value, yaml_parser_t* const parser, yaml_event_t* cur);
static void delete_struct__field(struct field* const value);
static char* construct_struct__root(struct root* const value, yaml_parser_t* const parser, yaml_event_t* cur);
static void delete_struct__root(struct root* const value);
static bool convert_to_enum__value_type(const char* const value, enum value_type* const result) {
  static const int8_t table[][22] = {
      {-1, -1, -1, 1, -1, -1, -1, -1, -1, 5, -1, -1, -1, -1, 14, -1, -1, -1, -1, 8, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 4, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 6, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 9, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 15, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 16, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
  };
  int8_t res;
  walk(table, (unsigned char*)value, 96, 117, res);
  switch(res) {
      case 4:
        *result = CHAR_VALUE;
          break;
      case 7:
        *result = INT_VALUE;
          break;
      case 13:
        *result = STRING_VALUE;
          break;
      case 17:
        *result = NO_VALUE;
          break;
    default: return false;
  }
  return true;
}

static char* construct_enum__value_type(enum value_type* const value, yaml_parser_t* const parser, yaml_event_t* cur) {
  (void)parser;
  if (cur->type != YAML_SCALAR_EVENT) {
    return wrong_event_error(YAML_SCALAR_EVENT, cur);
  }
  char* ret;
  if (convert_to_enum__value_type((const char*)cur->data.scalar.value, value)) {
    ret = NULL;
  } else {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    ret = render_error(cur, "unknown enum value: %s", escaped_len, escaped);
    free(escaped);
  }
  return ret;
}


static char* construct_struct__field(struct field* const value, yaml_parser_t* const parser, yaml_event_t* cur) {
  yaml_char_t* tag;
  switch(cur->type) {
    case YAML_SCALAR_EVENT:
      tag = cur->data.scalar.tag;
      break;
    case YAML_MAPPING_START_EVENT:
      tag = cur->data.mapping_start.tag;
      break;
    case YAML_SEQUENCE_START_EVENT:
      tag = cur->data.sequence_start.tag;
      break;
    default:
      return render_error(cur, "expected tagged event, got %s",14, event_spelling(cur->type));
  }
  if (tag[0] != '!' || tag[1] == '\0') {
    return render_error(cur, "value for tagged union must have specific local tag, got \"%s\"", strlen((const char*)tag), (const char*)tag);
  }
  bool res = convert_to_enum__value_type((const char*)(tag + 1), &value->type);
  if (!res) {
    return render_error(cur, "not a valid tag: \"%s\"", strlen((const char*)tag), (const char*)tag);
  }
  char* ret = NULL;
  switch(value->type) {
    case CHAR_VALUE:
      ret = construct_char(&value->c, parser, cur);
      break;
    case INT_VALUE:
      ret = construct_int(&value->i, parser, cur);
      break;
    case STRING_VALUE:
      ret = construct_string(&value->s, parser, cur);
      break;
    case NO_VALUE:
      if (cur->type != YAML_SCALAR_EVENT ||
          (cur->data.scalar.value[0] != '\0')) {
        ret = render_error(cur, "tag %s does not allow content", strlen((const char*)tag), (const char*)tag);
      } else ret = NULL;
  }
  return ret;
}

static void delete_struct__field(struct field* const value) {
  switch(value->type) {
    case CHAR_VALUE: break;
    case INT_VALUE: break;
    case STRING_VALUE: break;
    case NO_VALUE: break;
  }
}

static char* construct_struct__root(struct root* const value, yaml_parser_t* const parser, yaml_event_t* cur) {
  if (cur->type != YAML_SEQUENCE_START_EVENT) {
    return wrong_event_error(YAML_SEQUENCE_START_EVENT, cur);
  }
  value->data = malloc(16 * sizeof(struct field));
  value->count = 0;
  value->capacity = 16;
  yaml_event_t event;
  yaml_parser_parse(parser, &event);
  while (event.type != YAML_SEQUENCE_END_EVENT) {
    struct field* item;
    APPEND(value, item);
    char* ret = construct_struct__field(item, parser, &event);
    yaml_event_delete(&event);
    if (ret) {
      free(item);
      value->count--;
    delete_struct__root(value);
      return ret;
    }
    yaml_parser_parse(parser, &event);
  }
  yaml_event_delete(&event);
  return NULL;
}
static void delete_struct__root(struct root* const value) {
  for(size_t i = 0; i < value->count; ++i) {
    delete_struct__field(&value->data[i]);
  }
  free(value->data);
}
char* load_one(struct root* value, yaml_parser_t* parser) {
  yaml_event_t event;
  yaml_parser_parse(parser, &event);
  if (event.type == YAML_STREAM_START_EVENT) {
    yaml_event_delete(&event);
    yaml_parser_parse(parser, &event);
  }
  if (event.type != YAML_DOCUMENT_START_EVENT) {
    yaml_event_delete(&event);
    return wrong_event_error(YAML_DOCUMENT_START_EVENT, &event);
  }
  yaml_event_delete(&event);
  yaml_parser_parse(parser, &event);
  char* ret = construct_struct__root(value, parser, &event);
  yaml_event_delete(&event);
  yaml_parser_parse(parser, &event); // assume document end
  yaml_event_delete(&event);
  return ret;
}
