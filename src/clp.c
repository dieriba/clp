#include "clp.h"

#include "converter.h"
#include "d_general_lib.h"
#include "d_unordered_map.h"
#include "free.h"
#include "parse.h"
#include "shared.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#define NO_DESC "no associated description"
#define START_OPT_CHAR '-'

static bool is_valid_short(char shrt)
{
    return isalnum(shrt);
}

Command *clp_new_command(int code, char *name, char *description)
{
    Command *command = malloc(sizeof(Command));

    if (command == NULL)
        clp_exit_out_of_memory();

    assert(name != NULL);

    command->name           = d_string_view_from_c_string(name);
    command->description    = description == NULL ? NO_DESC : description;
    command->code           = code;
    command->parent_command = NULL;

    if (d_dyn_array_default_ptr_arr_init(&command->sub_commands, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK || d_dyn_array_default_ptr_arr_init(&command->options, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK ||
        d_dyn_array_default_ptr_arr_init(&command->extra, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK || d_dyn_array_default_ptr_arr_init(&command->operands, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK)
        clp_exit_out_of_memory();
    return command;
}

Option *clp_new_option_raw(char *long_name, char *short_name, char *description, bool has_default_value, OptAction action, Value value, Type type, bool required, bool global)
{
    assert(long_name != NULL || short_name != NULL);

    Option *opt = malloc(sizeof(Option));
    if (opt == NULL)
        clp_exit_out_of_memory();
    char shrt_name = FLAG_SHORT_OPT_NOT_SET;
    opt->long_name = d_string_view_from_c_string(long_name);

    if (short_name)
    {
        if (is_valid_short(*short_name) == false)
            clp_invalid_arg_exit("'-%c' is not a valid option name\na short option must be a single alphanumeric character [a-z A-Z 0-9]\n", *short_name);
        shrt_name = *short_name;
    }
    if (long_name)
    {
        if (PSEUDO_FAST_STRCMP(long_name, HELP_OPT))
            clp_invalid_arg_exit("%s is a reserved option\n", long_name);
        exit_if_not_valid_long_opt_name(opt->long_name);
    }

    if (action == OPT_ACT_COUNT && type != TYPE_USIZE)
    {
        if (long_name)
            clp_eprint("option '--%s' has action 'count' but type '%s' is not valid\n", long_name, type_to_str(type));
        else
            clp_eprint("option '-%c' has action 'count' but type '%s' is not valid\n", shrt_name, type_to_str(type));
        clp_eprint_exit("count action requires an integer type [u8 u16 u32 u64]\n");
    }

    if (has_default_value == false && action == OPT_ACT_LIST)
    {
        if (d_dyn_array_init(&opt->value.value_list, sizeof(DStringView), 1, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK)
            clp_exit_out_of_memory();
    }
    else if (has_default_value == false && action == OPT_ACT_KV)
    {
        if (d_unordered_map_init_not_owned_d_string_view_key(&opt->value.value_kv, sizeof(DStringView), 1, NULL))
            clp_exit_out_of_memory();
    }
    else
        opt->value = value;

    opt->value_set         = false;
    opt->action            = action;
    opt->short_name        = shrt_name;
    opt->description       = description == NULL ? NO_DESC : description;
    opt->has_default_value = has_default_value;
    opt->type              = type;
    opt->required          = required;
    opt->global            = global;
    opt->has_args          = !(type == TYPE_BOOL || action == OPT_ACT_COUNT);
    return opt;
}

void clp_init_operand_raw(Operand *operand, char *name, char *description, bool has_default_value, OperandAction action, Value value, Type type, bool required)
{
    assert(operand != NULL);
    assert(name != NULL);

    operand->name = d_string_view_from_c_string(name);
    exit_if_not_valid_long_opt_name(operand->name);

    if (has_default_value == false && action == OPERAND_ACT_LIST)
    {
        if (d_dyn_array_init(&operand->value.value_list, sizeof(DStringView), 1, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK)
            clp_exit_out_of_memory();
    }
    else if (has_default_value == false && action == OPERAND_ACT_KV)
    {
        if (d_unordered_map_init_not_owned_d_string_view_key(&operand->value.value_kv, sizeof(DStringView), 1, NULL))
            clp_exit_out_of_memory();
    }
    else
        operand->value = value;

    operand->value_set         = false;
    operand->action            = action;
    operand->description       = description == NULL ? NO_DESC : description;
    operand->has_default_value = has_default_value;
    operand->type              = type;
    operand->required          = required;
}

static inline bool eq_short_opt(Option *opt, void *ctx)
{
    return opt->short_name == *(char *) ctx;
}

static inline bool eq_long_opt(Option *opt, void *ctx)
{
    return d_string_view_compare(opt->long_name, *((DStringView *) ctx));
}

static Option *get_option_by_predicate(Command *command, void *ctx, bool (*predicate)(Option *, void *))
{
    DDynArray *opts = &command->options;
    usize      size = d_dyn_array_get_size(opts);
    for (usize i = 0; i < size; i++)
    {
        Option *option = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        if (predicate(option, ctx) == true)
            return option;
    }
    return NULL;
}

static Operand *get_command_operand_by_name(Command *command, DStringView name)
{
    DDynArray *operands = &command->operands;
    usize      size     = d_dyn_array_get_size(operands);
    for (usize i = 0; i < size; i++)
    {
        Operand *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
        if (d_string_view_compare(operand->name, name))
            return operand;
    }
    return NULL;
}

Option *clp_get_option_by_short(Command *command, char shrt)
{
    assert(command != NULL);
    return get_option_by_predicate(command, &shrt, eq_short_opt);
}

Option *clp_get_option_by_long(Command *command, DStringView lng)
{
    assert(command != NULL);
    assert(lng.size > 0);
    return get_option_by_predicate(command, &lng, eq_long_opt);
}

Operand *clp_get_operand(Command *command, DStringView operand_name)
{
    assert(command != NULL);
    DDynArray *operands = &command->operands;
    usize      size     = d_dyn_array_get_size(operands);
    for (usize i = 0; i < size; i++)
    {
        Operand *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
        if (d_string_view_compare(operand->name, operand_name))
            return operand;
    }
    return NULL;
}

void clp_add_command_sub_command(Command *command, Command *sub_command)
{
    assert(command != NULL);
    assert(sub_command != NULL);

    if (d_dyn_array_get_size(&(command->operands)) > 0)
        clp_invalid_arg_exit("command '%s' cannot have both operands and subcommands\n", command->name.data);

    sub_command->parent_command = command;
    if (d_dyn_array_push_back_ptr(&command->sub_commands, sub_command) != D_OK)
        clp_exit_out_of_memory();
}

void clp_add_command_option(Command *command, Option *command_option)
{
    assert(command != NULL);
    assert(command_option != NULL);

    if (clp_get_option_by_long(command, command_option->long_name))
        clp_eprint_exit("command %s: option '--%s' already registered\n", command->name.data, command_option->long_name.data);
    if (clp_get_option_by_short(command, command_option->short_name))
        clp_eprint_exit("command %s: option '-%c' already registered\n", command->name.data, command_option->short_name);
    if (d_dyn_array_push_back_ptr(&command->options, command_option) != D_OK)
        clp_exit_out_of_memory();
}

void clp_add_command_operand(Command *command, Operand *command_operand)
{
    assert(command != NULL);
    assert(command_operand != NULL);

    if (d_dyn_array_get_size(&(command->sub_commands)) > 0)
        clp_invalid_arg_exit("command '%s' cannot have both operands and subcommands\n", command->name.data);

    DDynArray *ops          = &command->operands;
    usize      size         = d_dyn_array_get_size(ops);
    Operand   *last_operand = size == 0 ? NULL : d_dyn_array_get_elem_deref_addr_at_safe(ops, size - 1);

    if (get_command_operand_by_name(command, command_operand->name))
        clp_eprint("warning: operand name '%s' already used, consider a more descriptive name\n", command_operand->name.data);
    else if (last_operand != NULL && (last_operand->required == false && command_operand->required == true))
        clp_eprint_exit("command %s: required operand '%s' cannot follow optional operand '%s'\n", command->name.data, command_operand->name.data, last_operand->name.data);
    if (d_dyn_array_push_back_ptr(&command->operands, command_operand) != D_OK)
        clp_exit_out_of_memory();
}

void clp_parse_args(Command *root, char **argv, Command **command)
{
    assert(root != NULL);
    assert(command != NULL);
    assert(argv != NULL && *argv != NULL);
    parse(root, argv, command);
}

void clp_cleanup(Command *root)
{
    assert(root != NULL);
    free_command(root);
}
