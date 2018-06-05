
#ifndef _TEST_COMMON_H
#define _TEST_COMMON_H

#define ASSERT_EQUALS_BOOL(expected, actual, res) {\
  if ((expected != (actual))) {\
    fprintf(stderr, "wrong value for \"%s\": expected %s, got %s\n", #actual, \
            expected ? "true" : "false", actual ? "true" : "false");\
    (res) = false;\
  }\
}

#define ASSERT_EQUALS_CHAR(expected, actual, res) {\
  if ((expected) != (actual)) {\
    fprintf(stderr, "wrong value for \"%s\": expected %c, got %c\n", #actual, \
            expected, actual);\
    (res) = false;\
  }\
}

#define ASSERT_EQUALS_INT(expected, actual, res) {\
  if ((expected) != (actual)) {\
    fprintf(stderr, "wrong value for \"%s\": expected %d, got %d\n", #actual, \
            expected, actual);\
    (res) = false;\
  }\
}

#define ASSERT_EQUALS_SIZE(expected, actual, res) {\
  if ((expected) != (actual)) {\
    fprintf(stderr, "wrong value for \"%s\": expected %zu, got %zu\n", #actual, \
            expected, actual);\
    (res) = false;\
  }\
}

#define ASSERT_EQUALS_FLOAT(expected, actual, res) {\
  if (abs((expected) - (actual) > 0.0000001)) {\
    fprintf(stderr, "wrong value for \"%s\": expected %.4f, got %.4f\n", #actual, \
            expected, actual);\
    (res) = false;\
  }\
}

#define ASSERT_EQUALS_STRING(expected, actual, res) {\
  if (strcmp((expected), (actual)) != 0) {\
    fprintf(stderr, "wrong value for \"%s\": expected %s, got %s\n", #actual, \
            expected, actual);\
    (res) = false;\
  }\
}

#define ASSERT_EQUALS_ENUM(expected, actual, res, repr) {\
  if ((expected) != (actual)) {\
    fprintf(stderr, "wrong value for \"%s\": expected %s, got %s\n", #actual, \
        repr[(int)(expected)], repr[(int)(actual)]);\
    (res) = false;\
  }\
}

#define ASSERT_NOT_NULL(actual, res) {\
  if ((actual) == NULL) {\
    fprintf(stderr, "missing value for \"%s\".\n", #actual);\
    (res) = false;\
  }\
}

#define ASSERT_NULL(actual, res) {\
  if ((actual) != NULL) {\
    fprintf(stderr, "got value for \"%s\" (expected NULL).\n", #actual);\
    (res) = false;\
  }\
}

#endif