#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmdline_config.h"

void usage(const char *executable) {
  fprintf(stdout, "Usage: %s [options] file\n", executable);
  fputs("  options:\n"
        "    -o directory       writes output files to $directory (default: .)\n"
        "    -r name            expects the root type to be named $name.\n"
        "                       default: \"root\"\n"
        "    -n name            names output files $name.h and $name.c .\n"
        "                       default: $file without extension.\n", stdout);
}

const char *last_index(const char *string, char c) {
  char *res = NULL;
  char *next = strchr(string, c);
  while (next != NULL) {
    res = next;
    next = strchr(res + 1, c);
  }
  return res;
}

cmdline_result_t process_cmdline_args(int argc, const char* argv[],
                                      cmdline_config_t* config) {
  const char* target_dir = NULL;
  config->root_name = NULL;
  const char* output_name = NULL;
  config->input_file_path = NULL;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (i == argc - 1 && argv[i][1] != 'h') {
        fprintf(stderr, "switch %s is missing value!\n", argv[i]);
        usage(argv[0]);
        return ARGS_ERROR;
      }

      switch (argv[i][1]) {
        case 'o':
          if (target_dir != NULL) {
            fputs("duplicate -o switch!\n", stderr);
            usage(argv[0]);
            return ARGS_ERROR;
          } else {
            target_dir = argv[++i];
          }
          break;
        case 'r':
          if (config->root_name != NULL) {
            fputs("duplicate -r switch!\n", stderr);
            usage(argv[0]);
            return ARGS_ERROR;
          } else {
            config->root_name = argv[++i];
          }
          break;
        case 'n':
          if (output_name != NULL) {
            fputs("duplicate -n switch!\n", stderr);
            usage(argv[0]);
            return ARGS_ERROR;
          } else {
            output_name = argv[++i];
          }
          break;
        case 'h':
          usage(argv[0]);
          return ARGS_HELP;
        default:
          fprintf(stderr, "unknown switch: '%s'\n", argv[i]);
          usage(argv[0]);
          return ARGS_ERROR;
      }
    } else if (config->input_file_path != NULL) {
      fprintf(stderr, "unexpected parameter: '%s'", argv[i]);
      usage(argv[0]);
      return ARGS_ERROR;
    } else {
      config->input_file_path = argv[i];
    }
  }
  if (target_dir == NULL) target_dir = ".";
  if (config->root_name == NULL) config->root_name = "struct root";
  if (config->input_file_path == NULL) {
    fputs("missing input file\n", stderr);
    usage(argv[0]);
    return ARGS_ERROR;
  }
  if (output_name == NULL) {
    const char *dot = last_index(config->input_file_path, '.');
    const char *slash = last_index(config->input_file_path, '/');
    const char *start = slash == NULL ? config->input_file_path : slash + 1;
    size_t len = (dot == NULL || dot < slash) ? strlen(start) : dot - start;
    char *dest = malloc(len + 9);
    memcpy(dest, start, len);
    strcpy(dest + len, "_loading");
    output_name = dest;
  }

  size_t target_dir_len = strlen(target_dir);
  const size_t output_name_len = strlen(output_name);
  const size_t path_length = target_dir_len + output_name_len + 3;
  config->output_header_path = malloc(path_length);
  config->output_impl_path = malloc(path_length);
  memcpy(config->output_header_path, target_dir, target_dir_len);
  memcpy(config->output_impl_path, target_dir, target_dir_len);
  if (target_dir[target_dir_len - 1] != '/') {
    config->output_header_path[target_dir_len] = '/';
    config->output_impl_path[target_dir_len] = '/';
    ++target_dir_len;
  }
  memcpy(config->output_header_path + target_dir_len, output_name, output_name_len);
  strcpy(config->output_header_path + target_dir_len + output_name_len, ".h");
  memcpy(config->output_impl_path + target_dir_len, output_name, output_name_len);
  strcpy(config->output_impl_path + target_dir_len + output_name_len, ".c");
  config->input_file_name = last_index(config->input_file_path, '/');
  if (config->input_file_name == NULL)
    config->input_file_name = config->input_file_path;
  else ++config->input_file_name;
  config->output_header_name = config->output_header_path + target_dir_len;
  return ARGS_SUCCESS;
}