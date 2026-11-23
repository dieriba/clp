#ifndef PARSE_H
#define PARSE_H
#include "clp.h"

ClpStatus parse(Command *root, char **argv, Command **command, ClpError *err);
bool is_valid_long(DStringView lng);

#endif