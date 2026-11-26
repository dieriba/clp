#include "free.h"

#include "clp.h"
#include "d_dyn_array.h"
#include "d_types.h"

#include <stdlib.h>

void free_option(Option *option)
{
    free(option);
}

static void free_command_options(Command *root)
{
    DDynArray *opts = &root->options;
    for (usize i = 0; i < opts->array.size; i++)
    {
        Option *opt = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        free_option(opt);
    }
    d_dyn_array_destroy(&root->options);
}

void free_operand(Operand *operand)
{
    free(operand);
}

static void free_command_operands(Command *root)
{
    DDynArray *operands = &root->operands;
    for (usize i = 0; i < operands->array.size; i++)
    {
        Operand *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
        free_operand(operand);
    }

    d_dyn_array_destroy(&root->operands);
}

static void free_sub_commands(Command *root)
{
    DDynArray *sub_commands = &root->sub_commands;
    usize      size         = d_dyn_array_get_size(sub_commands);
    for (usize i = 0; i < size; i++)
    {
        Command *sub_cmd = d_dyn_array_get_elem_deref_addr_at_safe(sub_commands, i);
        free_command(sub_cmd);
    }
    d_dyn_array_destroy(&root->sub_commands);
}

void free_command(Command *command)
{
    if (command == NULL)
        return;

    free_sub_commands(command);
    free_command_options(command);
    free_command_operands(command);
    d_dyn_array_destroy(&command->extra);
    free(command);
}
