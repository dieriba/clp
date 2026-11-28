#include "free.h"

#include "clp.h"
#include "d_dyn_array.h"
#include "d_types.h"

#include <stdlib.h>

void free_option(clp_option_t *option)
{
    free(option);
}

static void free_command_options(clp_command_t *root)
{
    d_dyn_array_t *opts = &root->options;
    for (usize i = 0; i < opts->array.size; i++)
    {
        clp_option_t *opt = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        free_option(opt);
    }
    d_dyn_array_destroy(&root->options);
}

void free_operand(clp_operand_t *operand)
{
    free(operand);
}

static void free_command_operands(clp_command_t *root)
{
    d_dyn_array_t *operands = &root->operands;
    for (usize i = 0; i < operands->array.size; i++)
    {
        clp_operand_t *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
        free_operand(operand);
    }

    d_dyn_array_destroy(&root->operands);
}

static void free_sub_commands(clp_command_t *root)
{
    d_dyn_array_t *sub_commands = &root->sub_commands;
    usize          size         = d_dyn_array_get_size(sub_commands);
    for (usize i = 0; i < size; i++)
    {
        clp_command_t *sub_cmd = d_dyn_array_get_elem_deref_addr_at_safe(sub_commands, i);
        free_command(sub_cmd);
    }
    d_dyn_array_destroy(&root->sub_commands);
}

void free_command(clp_command_t *command)
{
    if (command == NULL)
        return;

    free_sub_commands(command);
    free_command_options(command);
    free_command_operands(command);
    d_dyn_array_destroy(&command->extra);
    free(command);
}
