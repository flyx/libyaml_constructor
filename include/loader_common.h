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
#include <stdarg.h>
#include <assert.h>

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
    typeof((list)->data) newlist = malloc(sizeof(*(list)->data) * (list)->capacity * 2); \
    memcpy(newlist, (list)->data, sizeof(*(list)->data) * (list)->capacity); \
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

#define PO10_LIMIT (SIZE_MAX/10)


size_t digits_count(size_t i) {
  size_t n = 1;
  size_t po10 = 10;
  while(i >= po10) {
    n++;
    if (po10 > PO10_LIMIT) break;
    po10 *= 10;
  }
  return n;
}

static char* render_error(yaml_event_t* event, const char* message,
                          size_t expected_param_length, ...) {
  static const char pos_template[] = "l. %zu, c. %zu: ";
  size_t expected_pos_len = sizeof(pos_template) - 7 + // placeholder + terminator
                            digits_count(event->start_mark.line) +
                            digits_count(event->start_mark.column);
  char* buffer = malloc(expected_pos_len + expected_param_length +
                        strlen(message) + 1);
  int pos_len = sprintf(buffer, pos_template, event->start_mark.line,
                          event->start_mark.column);
  assert(pos_len == expected_pos_len);
  va_list args;
  va_start(args, expected_param_length);
  vsprintf(buffer + expected_pos_len, message, args);
  va_end(args);
  return buffer;
}

static char* wrong_event_error(yaml_event_type_t expected,
                               yaml_event_t* actual) {
  return render_error(actual, "expected %s, got %s", 14 + 14,
      event_spelling(actual->type), event_spelling(expected));
}

static char* construct_int(int * const value, yaml_parser_t* const parser,
                      yaml_event_t* cur) {
  (void)parser;
  if (cur->type != YAML_SCALAR_EVENT) {
    return wrong_event_error(YAML_SCALAR_EVENT, cur);
  }
  char* result;
  long res = strtol((const char*)cur->data.scalar.value, &result, 10);
  if (*result != '\0') {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    char* buffer = render_error(cur, "cannot read %s as int!", escaped_len,
                                escaped);
    free(escaped);
    return buffer;
  } else if (res < INT_MIN || res > INT_MAX) {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    char* buffer = render_error(cur,
                                "int value of %s outside representable range!",
                                escaped_len, escaped);
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
    return wrong_event_error(YAML_SCALAR_EVENT, cur);
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
    return wrong_event_error(YAML_SCALAR_EVENT, cur);
  } else if (cur->data.scalar.value[0] == '\0' ||
             cur->data.scalar.value[1] != '\0') {
    size_t escaped_len;
    char* escaped = escape((const char*)cur->data.scalar.value, &escaped_len);
    char* buffer = render_error(cur, "expected single character, got %s",
                                escaped_len, escaped);
    free(escaped);
    return buffer;
  }
  *value = cur->data.scalar.value[0];
  return NULL;
}


#endif
