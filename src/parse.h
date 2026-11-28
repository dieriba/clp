#ifndef PARSE_H
#define PARSE_H
#include "clp.h"

clp_status_t parse(clp_t *clp, clp_command_t *root, char **argv, clp_command_t **command, clp_error_t *err);
bool         is_valid_long(d_string_view_t lng);

#endif