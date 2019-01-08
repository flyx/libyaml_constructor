#include <yaml_constructor.h>

#include <stdint.h>
#include <math.h>
#include <limits.h>
#include <stdarg.h>
#include <assert.h>
#include <yaml_loader.h>

char* yaml_constructor_escape(const char* const string, size_t* const size) {
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

const char* yaml_constructor_event_spelling(yaml_event_type_t type) {
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

static size_t digits_count(size_t i) {
	size_t n = 1;
	size_t po10 = 10;
	while (i >= po10) {
		n++;
		if (po10 > PO10_LIMIT) break;
		po10 *= 10;
	}
	return n;
}

#define DEFINE_INT_CONSTRUCTOR(name, value_type, min, max)\
bool name(value_type *const value, yaml_loader_t *const loader,\
                  yaml_event_t *cur) {\
  if (!yaml_constructor_check_event_type(loader, cur, YAML_SCALAR_EVENT))\
    return false;\
  char* result;\
  long long res = strtoll((const char*)cur->data.scalar.value, &result, 10);\
  if (*result != '\0' || res < min || res > max) {\
    const char typename[] = #value_type;\
    loader->error_info.expected = malloc(sizeof(typename));\
    if (loader->error_info.expected == NULL) {\
      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\
      yaml_event_delete(cur);\
    } else {\
      loader->error_info.type = YAML_LOADER_ERROR_VALUE;\
      memcpy(loader->error_info.expected, typename, sizeof(typename));\
      loader->error_info.event = *cur;\
    }\
    return false;\
  }\
  *value = (value_type)res;\
  return true;\
}

DEFINE_INT_CONSTRUCTOR(yaml_construct_short, short, SHRT_MIN, SHRT_MAX)
DEFINE_INT_CONSTRUCTOR(yaml_construct_int, int, INT_MIN, INT_MAX)
DEFINE_INT_CONSTRUCTOR(yaml_construct_long, long, LONG_MIN, LONG_MAX)
DEFINE_INT_CONSTRUCTOR(yaml_construct_long_long, long long, LLONG_MIN,
	LLONG_MAX)

#define DEFINE_UNSIGNED_CONSTRUCTOR(name, value_type, max) \
bool name(value_type *const value, yaml_loader_t *const loader,\
                  yaml_event_t* cur) {\
  if (!yaml_constructor_check_event_type(loader, cur, YAML_SCALAR_EVENT)) \
    return false;\
  char* result;\
  unsigned long long res =\
      strtoull((const char*)cur->data.scalar.value, &result, 10);\
   if (*result != '\0' || res > max) {\
    const char typename[] = #value_type;\
    loader->error_info.expected = malloc(sizeof(typename));\
    if (loader->error_info.expected == NULL) {\
      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\
      yaml_event_delete(cur);\
    } else {\
      loader->error_info.type = YAML_LOADER_ERROR_VALUE;\
      memcpy(loader->error_info.expected, typename, sizeof(typename));\
      loader->error_info.event = *cur;\
    }\
    return false;\
  }\
  *value = (value_type)res;\
  return true;\
}

DEFINE_UNSIGNED_CONSTRUCTOR(yaml_construct_unsigned_char, unsigned char,
	UCHAR_MAX)
DEFINE_UNSIGNED_CONSTRUCTOR(yaml_construct_unsigned_short, unsigned short,
	USHRT_MAX)
DEFINE_UNSIGNED_CONSTRUCTOR(yaml_construct_unsigned, unsigned, UINT_MAX)
DEFINE_UNSIGNED_CONSTRUCTOR(yaml_construct_unsigned_long, unsigned long,
	ULLONG_MAX)
DEFINE_UNSIGNED_CONSTRUCTOR(yaml_construct_unsigned_long_long,
	unsigned long long, ULLONG_MAX)

 bool yaml_construct_string(char** const value, yaml_loader_t *const loader,
		yaml_event_t* cur) {
  if (!yaml_constructor_check_event_type(loader, cur, YAML_SCALAR_EVENT))
    return false;
	size_t len = strlen((char*)cur->data.scalar.value) + 1;
	*value = malloc(len);
	if (*value == NULL) {
	  loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;
	  yaml_event_delete(cur);
	  return false;
	}
	memcpy(*value, cur->data.scalar.value, len);
	return true;
}

bool yaml_construct_char(char *const value, yaml_loader_t *const loader,
                         yaml_event_t* cur) {
  if (!yaml_constructor_check_event_type(loader, cur, YAML_SCALAR_EVENT)) {
    return false;
  } else if (cur->data.scalar.value[0] == '\0' ||
             cur->data.scalar.value[1] != '\0') {
    const char typename[] = "char";
    loader->error_info.expected = malloc(sizeof(typename));
    if (loader->error_info.expected == NULL) {
      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;
      yaml_event_delete(cur);
    } else {
      loader->error_info.type = YAML_LOADER_ERROR_VALUE;
      memcpy(loader->error_info.expected, typename, sizeof(typename));
      loader->error_info.event = *cur;
    }
    return false;
  }
	*value = cur->data.scalar.value[0];
	return true;
}

bool yaml_construct_bool(bool *const value, yaml_loader_t *const loader,
	yaml_event_t* cur) {
  if (!yaml_constructor_check_event_type(loader, cur, YAML_SCALAR_EVENT)) {
    return false;
  } else if (strcmp("true", (const char*)cur->data.scalar.value) == 0) {
		*value = true;
	} else if (strcmp("false", (const char*)cur->data.scalar.value) == 0) {
		*value = false;
	} else {
    const char typename[] = "bool";
    loader->error_info.expected = malloc(sizeof(typename));
    if (loader->error_info.expected == NULL) {
      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;
      yaml_event_delete(cur);
    } else {
      loader->error_info.type = YAML_LOADER_ERROR_VALUE;
      memcpy(loader->error_info.expected, typename, sizeof(typename));
      loader->error_info.event = *cur;
    }
    return false;
	}
	return true;
}

#define DEFINE_FP_CONSTRUCTOR(name, value_type, overflow, func) \
bool name(value_type *const value, yaml_loader_t *const loader,\
                  yaml_event_t* cur) {\
  if (!yaml_constructor_check_event_type(loader, cur, YAML_SCALAR_EVENT))\
    return false;\
  char* end_ptr;\
  *value = func((const char*)cur->data.scalar.value, &end_ptr);\
  if (*end_ptr != '\0' || *value == (overflow)) {\
    const char typename[] = #value_type;\
    loader->error_info.expected = malloc(sizeof(typename));\
    if (loader->error_info.expected == NULL) {\
      loader->error_info.type = YAML_LOADER_ERROR_OUT_OF_MEMORY;\
      yaml_event_delete(cur);\
    } else {\
      loader->error_info.type = YAML_LOADER_ERROR_VALUE;\
      memcpy(loader->error_info.expected, typename, sizeof(typename));\
      loader->error_info.event = *cur;\
    }\
    return false;\
  }\
  return true;\
}

DEFINE_FP_CONSTRUCTOR(yaml_construct_float, float, HUGE_VALF, strtof)
DEFINE_FP_CONSTRUCTOR(yaml_construct_double, double, HUGE_VAL, strtod)
DEFINE_FP_CONSTRUCTOR(yaml_construct_long_double, long double, HUGE_VALL,
	strtold)