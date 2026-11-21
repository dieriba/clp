#ifndef PRINT_H
#define PRINT_H

#include "clp.h"

#include <stdio.h>

void      print_usage_line(FILE *stream, Command *command);
ClpStatus check_help_or_invalid_opt(Command *root, Option *opt, char *prefix, const char *name, usize len, ClpError *err);
ClpStatus check_required_args(Command *root, ClpError *err);
#endif