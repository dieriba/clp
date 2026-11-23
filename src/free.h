#ifndef FREE_H
#define FREE_H

#include "clp.h"

void free_command(Command *command);
void free_option(Option *option);
void free_operand(Operand *operand);
#endif