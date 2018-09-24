#ifndef YAML_LOADER_H
#define YAML_LOADER_H

#include <yaml.h>
#include <stdbool.h>

/**
 * List of possible errors that may have occurred.
 */
typedef enum {
  /**
   * No error occurred.
   */
  YAML_LOADER_ERROR_NONE,
  /**
   * A parser error occurred, i.e. there is an error in the input syntax.
   * Use the yaml_parser_t structure to obtain details on the error.
   */
  YAML_LOADER_ERROR_PARSER,
  /**
   * The YAML structure looks different than expected: A certain event type has
   * been expected, but a different event type has been encountered.
   *
   * event will be set to the violating event. expected will be
   * set to the event type that was expected in place of the actual event.
   */
  YAML_LOADER_ERROR_STRUCTURAL,
  /**
   * A key in a YAML mapping has been given twice.
   *
   * event will be set to the second key.
   */
  YAML_LOADER_ERROR_DUPLICATE_KEY,
  /**
   * A key in a YAML mapping is missing, but is required to be there.
   *
   * event will be set to the mapping start event, expected will be set to the
   * name of the missing key.
   */
  YAML_LOADER_ERROR_MISSING_KEY,
  /**
   * A given key in a YAML mapping cannot be mapped to a struct field.
   *
   * event will be set to the unknown key.
   */
  YAML_LOADER_ERROR_UNKNOWN_KEY,
  /**
   * The recent event has an invalid tag or misses a mandatory tag.
   *
   * event will be set to the violating event, expected_type to the name
   * of the expected type.
   */
  YAML_LOADER_ERROR_TAG,
  /**
   * The value of a YAML scalar value cannot be parsed into the expected type.
   *
   * event will be set to the violating event, expected to the
   * expected type.
   */
  YAML_LOADER_ERROR_VALUE,
  /**
   * Allocating memory has failed.
   */
  YAML_LOADER_ERROR_OUT_OF_MEMORY
} yaml_loader_error_type_t;

typedef struct {
  struct {
    /**
     * This value is set to YAML_LOADER_ERROR_NONE until an error occurred. Its
     * value defines which of the other values inside this struct hold valid
     * values.
     */
    yaml_loader_error_type_t type;
    /**
     * Event on which the error occurred, if any
     */
    yaml_event_t event;
    /**
     * Expected event type in place of the actual event, if any
     */
    yaml_event_type_t expected_event_type;
    /**
     * Expected type (on YAML_LOADER_ERROR_STRUCTURAL)
     * or field name (on YAML_LOADER_ERROR_MISSING_KEY)
     */
     char *expected;
  } error_info;

  /**
   * The YAML parser used for loading. May be used to inquire about details of
   * YAML parser errors. Do not call YAML functionality on this directly.
   */
  yaml_parser_t *parser;

  /**
   * private values, do not touch
   */
  struct {
    bool external_parser;
  } internal;
} yaml_loader_t;

/**
 * Initialize the given loader to read the given file. If successful, it is the
 * caller's responsibility to destroy the loader with yaml_loader_destroy.
 * @return true on success, false on failure.
 */
bool yaml_loader_init_file(yaml_loader_t *loader, FILE *input);

/**
 * Initialize the given loader to read the given string. If successful, it is
 * the caller's responsibility to destroy the loader with yaml_loader_destroy.
 * @return true on success, false on failure.
 */
bool yaml_loader_init_string(yaml_loader_t *loader, const unsigned char *input,
                             size_t size);

/**
 * Initialize the given loader to use the given parser. The parser may already
 * have read documents successfully, the next event must be a document start or
 * a stream start event.
 *
 * If the loader is initialized with this function, it will not delete the
 * parser on deletion!
 */
bool yaml_loader_init_parser(yaml_loader_t *loader, yaml_parser_t *parser);

/**
 * Destroys a loader that has successfully been initialized.
 */
void yaml_loader_delete(yaml_loader_t *loader);

#endif
