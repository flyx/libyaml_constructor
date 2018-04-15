//
//  yaml_deserializer.h
//  libheroes
//
//  Created by Felix Krause on 02.04.18.
//

#ifndef LIBYAML_DESERIALIZER_H
#define LIBYAML_DESERIALIZER_H

#include <stdint.h>
#include <stdlib.h>
#include <yaml.h>
#include <limits.h>

static int8_t walk(const int8_t table[][256], const char* const name) {
  int8_t pos = 0;
  for(const char* cur = name; *cur != '\0'; ++cur) {
    pos = table[pos][*cur];
  }
  return pos;
}

static char* escape(const char* const string, size_t* const size) {
  size_t needed = 0;
  for (const char* ptr = string; *ptr != '\0'; ++ptr) {
    needed += (*ptr == '\t' || *ptr == '\n' || *ptr == '\r' || *ptr == '\\' ||
               *ptr == '\"') ? 2 : 1;
  }
  char* res = malloc(needed + 3);
  *res = '\"';
  char* resptr = res + 1;
  if (size != NULL) *size = needed;
  for (const char* ptr = string; *ptr != '\0'; ptr++) {
    switch (*ptr) {
      case '\t': *resptr++ = '\\'; *resptr++ = 't'; break;
      case '\n': *resptr++ = '\\'; *resptr++ = 'n'; break;
      case '\r': *resptr++ = '\\'; *resptr++ = 'r'; break;
      case '\\': *resptr++ = '\\'; *resptr++ = '\\'; break;
      case '\"': *resptr++ = '\\'; *resptr++ = '\"'; break;
      default: *resptr++ = *ptr; break;
    }
  }
  *resptr++ = '\"';
  *resptr = '\0';
  return res;
}

#define APPEND(list, ptr) { \
  if ((list)->count == (list)->capacity) { \
    typeof(list->data) newlist = malloc(sizeof(*list->data) * (list)->capacity * 2); \
    memcpy(newlist, (list)->data, sizeof(*list->data) * (list)->capacity); \
    free((list)->data); \
    (list)->data = newlist; \
    (list)->capacity *= 2; \
  } \
  (ptr) = &((list)->data[(list)->count++]); \
}

static const char* event_spelling(yaml_event_type_t type) {
  switch (type) {
    case YAML_STREAM_START_EVENT:   return "STREAM_START";
    case YAML_STREAM_END_EVENT:     return "STREAM_END";
    case YAML_DOCUMENT_START_EVENT: return "DOCUMENT_START";
    case YAML_DOCUMENT_END_EVENT:   return "DOCUMENT_END";
    case YAML_MAPPING_START_EVENT:  return "MAPPING_START";
    case YAML_MAPPING_END_EVENT:    return "MAPPING_END";
    case YAML_SEQUENCE_START_EVENT: return "SEQUENCE_START";
    case YAML_SEQUENCE_END_EVENT:   return "SEQUENCE_END";
    case YAML_SCALAR_EVENT:         return "SCALAR_EVENT";
    case YAML_ALIAS_EVENT:          return "ALIAS_EVENT";
    default:                        return "NO_EVENT";
  }
}

static char* wrong_event_error(yaml_event_type_t expected,
                               yaml_event_type_t actual) {
  char* buffer = malloc(16 + 14 + 14); // template + longest possible spelling
  sprintf(buffer, "expected %s, got %s", event_spelling(expected),
          event_spelling(actual));
  return buffer;
}

static char* construct_int(int * const value, yaml_parser_t* const parser,
                      yaml_event_t* cur) {
  (void)parser;
  if (cur->type != YAML_SCALAR_EVENT) {
    return wrong_event_error(YAML_SCALAR_EVENT, cur->type);
  }
  char* result;
  long res = strtol((const char*)cur->data.scalar.value, &result, 10);
  if (*result != '\0') {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    char* buffer = malloc(21 + escaped_len);
    sprintf(buffer, "cannot read %s as int!", escaped);
    free(escaped);
    return buffer;
  } else if (res < INT_MIN || res > INT_MAX) {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    char* buffer = malloc(43 + escaped_len);
    sprintf(buffer, "int value of %s outside representable range!", escaped);
    free(escaped);
    return buffer;
  }
  *value = (int)res;
  return NULL;
}

static char* construct_string(char** const value, yaml_parser_t* const parser,
                         yaml_event_t* cur) {
  (void)parser;
  if (cur->type != YAML_SCALAR_EVENT) {
    return wrong_event_error(YAML_SCALAR_EVENT, cur->type);
  }
  size_t len = strlen((char*) cur->data.scalar.value) + 1;
  *value = malloc(len);
  memcpy(*value, cur->data.scalar.value, len);
  return NULL;
}

static char* construct_char(char* const value, yaml_parser_t* const parser,
                       yaml_event_t* cur) {
  (void)parser;
  if (cur->type != YAML_SCALAR_EVENT) {
    return wrong_event_error(YAML_SCALAR_EVENT, cur->type);
  } else if (cur->data.scalar.value[0] == '\0' ||
             cur->data.scalar.value[1] != '\0') {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    char* buffer = malloc(32 + escaped_len);
    sprintf(buffer, "expected single character, got %s", escaped);
    free(escaped);
    return buffer;
  }
  *value = cur->data.scalar.value[0];
  return NULL;
}


#endif
