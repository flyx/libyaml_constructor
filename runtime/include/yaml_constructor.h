#ifndef YAML_CONSTRUCTOR_H
#define YAML_CONSTRUCTOR_H

#include <yaml.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define YAML_CONSTRUCTOR_WALK(table, name, min, max, result)\
  int8_t walk__pos = 0;\
  for(unsigned const char* walk__cur = name; *walk__cur != '\0' &&\
      walk__pos != -1; ++walk__cur) {\
    walk__pos = (table)[walk__pos][(*walk__cur < (min)) ? 0 :\
        ((*walk__cur > (max)) ? (max) : *walk__cur) - (min)];\
  }\
  (result) = walk__pos

char* yaml_constructor_escape(const char* const string, size_t* const size);

#define YAML_CONSTRUCTOR_APPEND(list, ptr) { \
  if ((list)->count == (list)->capacity) { \
    void* const newlist = malloc(sizeof(*(list)->data) * (list)->capacity * 2);\
    memcpy(newlist, (list)->data, sizeof(*(list)->data) * (list)->capacity); \
    free((list)->data); \
    (list)->data = newlist; \
    (list)->capacity *= 2; \
  } \
  (ptr) = &((list)->data[(list)->count++]); \
}

const char* yaml_constructor_event_spelling(yaml_event_type_t type);

char* yaml_constructor_render_error(yaml_event_t *event, const char *message,
	size_t expected_param_length, ...);

char* yaml_constructor_wrong_event_error(yaml_event_type_t expected,
	yaml_event_t* actual);

char* yaml_construct_short(short *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_int(int *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_long(long *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_long_long(long long *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_unsigned_char(unsigned char *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_unsigned_short(unsigned short *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_unsigned(unsigned *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_unsigned_long(unsigned long *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_unsigned_long_long(unsigned long long *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_float(float *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_double(double *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_long_double(long double *const value,
	yaml_parser_t *const parser, yaml_event_t *cur);

char* yaml_construct_string(char** const value,
	yaml_parser_t *const parser, yaml_event_t* cur);

char* yaml_construct_char(char *const value, yaml_parser_t *const parser,
	yaml_event_t* cur);

char* yaml_construct_bool(bool *const value, yaml_parser_t *const parser,
	yaml_event_t* cur);

#endif
