#ifndef PRINT_H
#define PRINT_H

#include "clp.h"

#include <stdio.h>

void         print_usage_line(FILE *stream, clp_command_t *command);
clp_status_t check_help_or_invalid_opt(clp_t *clp, clp_command_t *root, clp_option_t *opt, char *prefix, const char *name, usize len, clp_error_t *err);
clp_status_t check_required_args(clp_command_t *root, clp_error_t *err);
#endif