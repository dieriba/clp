#ifndef PRINT_H
#define PRINT_H

#include "clp.h"

#include <stdio.h>

bool print_command_required_args_if_miss_required_args(Command *root);
void print_usage_line(FILE *stream, Command *command);
void exit_help_or_if_invalid_opt(Command *root, Option *opt, char *prefix, const char *name, usize len);
#endif