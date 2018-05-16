#include <loader_common.h>
#include <stdbool.h>
#include "simple_loading.h"
static char* construct_enum__gender_t(enum gender_t* const value, yaml_parser_t* const parser, yaml_event_t* cur);
static bool convert_to_enum__gender_t(const char* const value, enum gender_t* const result);
static char* construct_struct__person(struct person* const value, yaml_parser_t* const parser, yaml_event_t* cur);
static void delete_struct__person(struct person* const value);
static char* construct_struct__person_list_s(struct person_list_s* const value, yaml_parser_t* const parser, yaml_event_t* cur);
static void delete_struct__person_list_s(struct person_list_s* const value);
static char* construct_struct__root(struct root* const value, yaml_parser_t* const parser, yaml_event_t* cur);
static void delete_struct__root(struct root* const value);
static bool convert_to_enum__gender_t(const char* const value, enum gender_t* const result) {
  static const int8_t table[][22] = {
      {-1, -1, -1, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, 1, -1, 11, -1, -1, -1, -1, -1, -1},
      {-1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 15, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
  };
  int8_t res;
  walk(table, (unsigned char*)value, 96, 117, res);
  switch(res) {
      case 4:
        *result = MALE;
          break;
      case 10:
        *result = FEMALE;
          break;
      case 15:
        *result = OTHER;
          break;
    default: return false;
  }
  return true;
}

static char* construct_enum__gender_t(enum gender_t* const value, yaml_parser_t* const parser, yaml_event_t* cur) {
  (void)parser;
  if (cur->type != YAML_SCALAR_EVENT) {
    return wrong_event_error(YAML_SCALAR_EVENT, cur);
  }
  char* ret;
  if (convert_to_enum__gender_t((const char*)cur->data.scalar.value, value)) {
    ret = NULL;
  } else {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    ret = render_error(cur, "unknown enum value: %s", escaped_len, escaped);
    free(escaped);
  }
  return ret;
}


static char* construct_struct__person(struct person* const value, yaml_parser_t* const parser, yaml_event_t* cur) {
  static const int8_t table[][20] = {
      {-1, 5, -1, -1, -1, -1, -1, 8, -1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1},
      {-1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
  };
  if (cur->type != YAML_MAPPING_START_EVENT) {
    return wrong_event_error(YAML_MAPPING_START_EVENT, cur);
  }
  yaml_event_t key;
  yaml_parser_parse(parser, &key);
  char* ret = NULL;
  bool found[] = {false, false, false};
  static const char* names[] = {"name", "age", "gender"};
  while(key.type != YAML_MAPPING_END_EVENT) {
    if (key.type != YAML_SCALAR_EVENT) {
      ret = wrong_event_error(YAML_SCALAR_EVENT, &key);
      break;
    }
    int8_t result;
    walk(table, key.data.scalar.value, 96, 115, result);
    yaml_event_t event;
    yaml_parser_parse(parser, &event);
    const char* const name = (const char*)key.data.scalar.value;
    switch(result) {
      case 4:
        if (found[0]) {
          size_t escaped_len;
          char* escaped = escape(name, &escaped_len);
          ret = render_error(&key, "duplicate key: %s", escaped_len, escaped);
          free(escaped);
        } else {
          found[0] = true;
          ret = construct_string(&value->name, parser, &event);
        }
        break;
      case 7:
        if (found[1]) {
          size_t escaped_len;
          char* escaped = escape(name, &escaped_len);
          ret = render_error(&key, "duplicate key: %s", escaped_len, escaped);
          free(escaped);
        } else {
          found[1] = true;
          ret = construct_int(&value->age, parser, &event);
        }
        break;
      case 13:
        if (found[2]) {
          size_t escaped_len;
          char* escaped = escape(name, &escaped_len);
          ret = render_error(&key, "duplicate key: %s", escaped_len, escaped);
          free(escaped);
        } else {
          found[2] = true;
          ret = construct_enum__gender_t(&value->gender, parser, &event);
        }
        break;
      default: {
          size_t escaped_len;
          char* escaped = escape(name, &escaped_len);
          ret = render_error(&key, "unknown field: %s", escaped_len,escaped);
          free(escaped);
        }
        break;
    }
    yaml_event_delete(&event);
    if (ret != NULL) break;
    yaml_event_delete(&key);
    yaml_parser_parse(parser, &key);
  }
  yaml_event_delete(&key);
  if (!ret) {
    for (size_t i = 0; i < sizeof(found); i++) {
      if (!found[i]) {
        const size_t name_len = strlen(names[i]);
        ret = render_error(cur, "missing value for field \"%s\"", name_len, names[i]);
        break;
      }
    }
  }
  return ret;
}

static void delete_struct__person(struct person* const value) {
  free(value->name);
}

static char* construct_struct__person_list_s(struct person_list_s* const value, yaml_parser_t* const parser, yaml_event_t* cur) {
  if (cur->type != YAML_SEQUENCE_START_EVENT) {
    return wrong_event_error(YAML_SEQUENCE_START_EVENT, cur);
  }
  value->data = malloc(16 * sizeof(struct person));
  value->count = 0;
  value->capacity = 16;
  yaml_event_t event;
  yaml_parser_parse(parser, &event);
  while (event.type != YAML_SEQUENCE_END_EVENT) {
    struct person* item;
    APPEND(value, item);
    char* ret = construct_struct__person(item, parser, &event);
    yaml_event_delete(&event);
    if (ret) {
      free(item);
      value->count--;
    delete_struct__person_list_s(value);
      return ret;
    }
    yaml_parser_parse(parser, &event);
  }
  yaml_event_delete(&event);
  return NULL;
}
static void delete_struct__person_list_s(struct person_list_s* const value) {
  for(size_t i = 0; i < value->count; ++i) {
    delete_struct__person(&value->data[i]);
  }
  free(value->data);
}

static char* construct_struct__root(struct root* const value, yaml_parser_t* const parser, yaml_event_t* cur) {
  static const int8_t table[][26] = {
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
  };
  if (cur->type != YAML_MAPPING_START_EVENT) {
    return wrong_event_error(YAML_MAPPING_START_EVENT, cur);
  }
  yaml_event_t key;
  yaml_parser_parse(parser, &key);
  char* ret = NULL;
  bool found[] = {false, false};
  static const char* names[] = {"symbol", "people"};
  while(key.type != YAML_MAPPING_END_EVENT) {
    if (key.type != YAML_SCALAR_EVENT) {
      ret = wrong_event_error(YAML_SCALAR_EVENT, &key);
      break;
    }
    int8_t result;
    walk(table, key.data.scalar.value, 97, 122, result);
    yaml_event_t event;
    yaml_parser_parse(parser, &event);
    const char* const name = (const char*)key.data.scalar.value;
    switch(result) {
      case 6:
        if (found[0]) {
          size_t escaped_len;
          char* escaped = escape(name, &escaped_len);
          ret = render_error(&key, "duplicate key: %s", escaped_len, escaped);
          free(escaped);
        } else {
          found[0] = true;
          ret = construct_char(&value->symbol, parser, &event);
        }
        break;
      case 12:
        if (found[1]) {
          size_t escaped_len;
          char* escaped = escape(name, &escaped_len);
          ret = render_error(&key, "duplicate key: %s", escaped_len, escaped);
          free(escaped);
        } else {
          found[1] = true;
          ret = construct_struct__person_list_s(&value->people, parser, &event);
        }
        break;
      default: {
          size_t escaped_len;
          char* escaped = escape(name, &escaped_len);
          ret = render_error(&key, "unknown field: %s", escaped_len,escaped);
          free(escaped);
        }
        break;
    }
    yaml_event_delete(&event);
    if (ret != NULL) break;
    yaml_event_delete(&key);
    yaml_parser_parse(parser, &key);
  }
  yaml_event_delete(&key);
  if (!ret) {
    for (size_t i = 0; i < sizeof(found); i++) {
      if (!found[i]) {
        const size_t name_len = strlen(names[i]);
        ret = render_error(cur, "missing value for field \"%s\"", name_len, names[i]);
        break;
      }
    }
  }
  return ret;
}

static void delete_struct__root(struct root* const value) {
  delete_struct__person_list_s(&value->people);
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
