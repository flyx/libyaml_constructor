
#ifndef _TEST_COMMON_H
#define _TEST_COMMON_H

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

#endif