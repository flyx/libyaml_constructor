#ifndef LIBHEROES_CMDLINE_CONFIG_H
#define LIBHEROES_CMDLINE_CONFIG_H

#include <stdbool.h>

typedef struct {
  char *output_impl_path;
  const char *root_name;
  char *output_header_path;
  const char *output_header_name;
  const char *input_file_path;
  const char *input_file_name;
} cmdline_config_t;

typedef enum {
  ARGS_SUCCESS, ARGS_ERROR, ARGS_HELP
} cmdline_result_t;

cmdline_result_t process_cmdline_args(int argc, const char* argv[],
                                      cmdline_config_t* config);

#endif //LIBHEROES_CMDLINE_CONFIG_H
