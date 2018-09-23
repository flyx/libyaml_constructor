#include <yaml_loader.h>

bool yaml_loader_init_file(yaml_loader_t *loader, FILE *input) {
  if (yaml_parser_initialize(&loader->parser) == 0) {
    return false;
  }
  yaml_parser_set_input_file(&loader->parser, input);
  loader->error_info.type = YAML_LOADER_ERROR_NONE;
  return true;
}

bool yaml_loader_init_string(yaml_loader_t *loader, const unsigned char *input,
                             size_t size) {
  if (yaml_parser_initialize(&loader->parser) == 0) {
    return false;
  }
  yaml_parser_set_input_string(&loader->parser, input, size);
  loader->error_info.type = YAML_LOADER_ERROR_NONE;
  return true;
}

/**
 * Destroys a loader that has successfully been initialized.
 */
void yaml_loader_destroy(yaml_loader_t *loader) {
  yaml_parser_delete(&loader->parser);
  switch (loader->error_info.type) {
    case YAML_LOADER_ERROR_TAG:
    case YAML_LOADER_ERROR_VALUE:
    case YAML_LOADER_ERROR_MISSING_KEY:
      free(loader->error_info.expected);
    case YAML_LOADER_ERROR_STRUCTURAL:
    case YAML_LOADER_ERROR_DUPLICATE_KEY:
    case YAML_LOADER_ERROR_UNKNOWN_KEY:
      yaml_event_delete(&loader->error_info.event);
      break;
    case YAML_LOADER_ERROR_NONE:
    case YAML_LOADER_ERROR_PARSER:
    case YAML_LOADER_ERROR_OUT_OF_MEMORY:
      break;
  }
}