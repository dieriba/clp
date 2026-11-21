#ifndef PARSE_H
#define PARSE_H
#include "clp.h"

ClpStatus parse(Command *root, char **argv, Command **command, ClpError *err);
void exit_if_not_valid_long_opt_name(DStringView long_opt);

#endif