#ifndef FREE_H
#define FREE_H

#include "clp.h"

void free_command(clp_command_t *command);
void free_option(clp_option_t *option);
void free_operand(clp_operand_t *operand);
#endif