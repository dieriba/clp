#include "clp.h"

#include "converter.h"
#include "d_general_lib.h"
#include "d_unordered_map.h"
#include "error.h"
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
    return isalnum((unsigned char) shrt) || shrt == '?';
}

#define LONG_NAME_RULE "a long option must start with a letter and contain only [a-z A-Z 0-9 -]\n"

ClpStatus clp_new_command(Command **out, int code, char *name, char *description, ClpValidator validator, void *ctx, ClpError *err)
{
    assert(out != NULL);
    assert(name != NULL);

    *out             = NULL;
    Command *command = malloc(sizeof(Command));
    if (command == NULL)
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");

    command->name           = d_string_view_from_c_string(name);
    command->description    = description == NULL ? NO_DESC : description;
    command->code           = code;
    command->parent_command = NULL;
    command->validator      = validator;
    command->ctx            = ctx;

    if (d_dyn_array_default_ptr_arr_init(&command->sub_commands, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK || d_dyn_array_default_ptr_arr_init(&command->options, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK ||
        d_dyn_array_default_ptr_arr_init(&command->extra, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK || d_dyn_array_default_ptr_arr_init(&command->operands, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK)
    {
        free_command(command);
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");
    }
    *out = command;
    return CLP_OK;
}

static const char *check_transform_combination(Type type, bool is_set, ClpChecker checker, ClpTransform transform)
{
    if (transform != NULL && type != TYPE_CUSTOM)
        return "a transform requires type 'custom'";
    if (transform == NULL && type == TYPE_CUSTOM)
        return "type 'custom' requires a transform";
    if (transform != NULL && is_set == false)
        return "a transform is only allowed with the 'set' action";
    if (transform != NULL && checker != NULL)
        return "a transform cannot be combined with a checker";
    return NULL;
}

ClpStatus clp_new_option_raw(Option **out, char *long_name, char *short_name, char *description, void *dest, OptAction action, Type type, bool required, bool global, ClpChecker checker,
                             ClpTransform transform, ClpError *err)
{
    assert(out != NULL);
    assert(long_name != NULL || short_name != NULL);
    assert(dest != NULL || type == TYPE_CUSTOM);

    *out           = NULL;
    char shrt_name = FLAG_SHORT_OPT_NOT_SET;

    if (short_name)
    {
        if (is_valid_short(*short_name) == false)
            return error_fail(err, CLP_ERR_INVALID_NAME, NULL, "invalid argument: '-%c' is not a valid option name\na short option must be a single alphanumeric character [a-z A-Z 0-9] or '?'\n",
                              *short_name);
        shrt_name = *short_name;
    }
    if (long_name)
    {
        if (PSEUDO_FAST_STRCMP(long_name, HELP_OPT))
            return error_fail(err, CLP_ERR_RESERVED_NAME, NULL, "invalid argument: %s is a reserved option\n", long_name);
        if (is_valid_long(d_string_view_from_c_string(long_name)) == false)
            return error_fail(err, CLP_ERR_INVALID_NAME, NULL, "'--%s' is not a valid option name\n" LONG_NAME_RULE, long_name);
    }

    if (action == OPT_ACT_COUNT && type != TYPE_USIZE)
    {
        if (long_name)
            return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '--%s' has action 'count' but type '%s' is not valid\ncount action requires an integer type [u8 u16 u32 u64]\n", long_name,
                              type_to_str(type));
        return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '-%c' has action 'count' but type '%s' is not valid\ncount action requires an integer type [u8 u16 u32 u64]\n", shrt_name,
                          type_to_str(type));
    }

    const char *bad_combination = check_transform_combination(type, action == OPT_ACT_SET, checker, transform);
    if (bad_combination)
    {
        if (long_name)
            return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '--%s': %s\n", long_name, bad_combination);
        return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '-%c': %s\n", shrt_name, bad_combination);
    }

    Option *opt = malloc(sizeof(Option));
    if (opt == NULL)
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");

    opt->long_name   = d_string_view_from_c_string(long_name);
    opt->action      = action;
    opt->short_name  = shrt_name;
    opt->description = description == NULL ? NO_DESC : description;
    opt->type        = type;
    opt->required    = required;
    opt->global      = global;
    opt->dest        = dest;
    opt->checker     = checker;
    opt->transform   = transform;
    opt->value_set   = false;
    opt->has_args    = !(type == TYPE_BOOL || action == OPT_ACT_COUNT);
    *out             = opt;
    return CLP_OK;
}

ClpStatus clp_new_operand_raw(Operand **out, char *name, char *description, void *dest, OperandAction action, Type type, bool required, ClpChecker checker, ClpTransform transform, ClpError *err)
{
    assert(out != NULL);
    assert(name != NULL);
    assert(dest != NULL || type == TYPE_CUSTOM);

    *out = NULL;
    if (is_valid_long(d_string_view_from_c_string(name)) == false)
        return error_fail(err, CLP_ERR_INVALID_NAME, NULL, "'--%s' is not a valid option name\n" LONG_NAME_RULE, name);

    const char *bad_combination = check_transform_combination(type, action == OPERAND_ACT_SET, checker, transform);
    if (bad_combination)
        return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "operand '<%s>': %s\n", name, bad_combination);

    Operand *operand = malloc(sizeof(Operand));
    if (operand == NULL)
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");

    operand->name        = d_string_view_from_c_string(name);
    operand->action      = action;
    operand->description = description == NULL ? NO_DESC : description;
    operand->type        = type;
    operand->dest        = dest;
    operand->required    = required;
    operand->checker     = checker;
    operand->transform   = transform;
    operand->value_set   = false;
    *out                 = operand;
    return CLP_OK;
}

static inline bool eq_short_opt(Option *opt, void *ctx)
{
    char c = *(char *) ctx;
    return c != (char) FLAG_SHORT_OPT_NOT_SET && opt->short_name == c;
}

static inline bool eq_long_opt(Option *opt, void *ctx)
{
    return opt->long_name.size > 0 && d_string_view_compare(opt->long_name, *((DStringView *) ctx));
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

ClpStatus clp_add_command_sub_command(Command *command, Command *sub_command, ClpError *err)
{
    assert(command != NULL);
    assert(sub_command != NULL);

    if (d_dyn_array_get_size(&(command->operands)) > 0)
        return error_fail(err, CLP_ERR_CONFLICT, command, "invalid argument: command '%s' cannot have both operands and subcommands\n", command->name.data);
    if (d_dyn_array_push_back_ptr(&command->sub_commands, sub_command) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, command, "out of memory\n");

    sub_command->parent_command = command;
    return CLP_OK;
}

ClpStatus clp_add_command_option(Command *command, Option *command_option, ClpError *err)
{
    assert(command != NULL);
    assert(command_option != NULL);

    if (clp_get_option_by_long(command, command_option->long_name))
        return error_fail(err, CLP_ERR_DUPLICATE, command, "command %s: option '--%s' already registered\n", command->name.data, command_option->long_name.data);
    if (clp_get_option_by_short(command, command_option->short_name))
        return error_fail(err, CLP_ERR_DUPLICATE, command, "command %s: option '-%c' already registered\n", command->name.data, command_option->short_name);
    if (d_dyn_array_push_back_ptr(&command->options, command_option) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, command, "out of memory\n");

    return CLP_OK;
}

ClpStatus clp_insert_opts(Command *command, const ClpOptionInfo *infos, usize count, ClpError *err)
{
    assert(command != NULL);
    assert(infos != NULL || count == 0);

    for (usize i = 0; i < count; i++)
    {
        const ClpOptionInfo *info = &infos[i];
        Option              *opt  = NULL;
        ClpStatus            status;

        status = clp_new_option_raw(&opt, info->long_name, info->short_name, info->description, info->dest, info->action, info->type, info->required, info->global, info->checker,
                                    info->transform, err);
        if (status != CLP_OK)
            return status;
        if ((status = clp_add_command_option(command, opt, err)) != CLP_OK)
        {
            clp_free_option(opt);
            return status;
        }
    }
    return CLP_OK;
}

ClpStatus clp_add_command_operand(Command *command, Operand *command_operand, ClpError *err)
{
    assert(command != NULL);
    assert(command_operand != NULL);

    if (d_dyn_array_get_size(&(command->sub_commands)) > 0)
        return error_fail(err, CLP_ERR_CONFLICT, command, "invalid argument: command '%s' cannot have both operands and subcommands\n", command->name.data);

    DDynArray *ops          = &command->operands;
    usize      size         = d_dyn_array_get_size(ops);
    Operand   *last_operand = size == 0 ? NULL : d_dyn_array_get_elem_deref_addr_at_safe(ops, size - 1);

    if (get_command_operand_by_name(command, command_operand->name))
        clp_print_warning("warning: operand name '%s' already used, consider a more descriptive name\n", command_operand->name.data);
    else if (last_operand != NULL && (last_operand->required == false && command_operand->required == true))
        return error_fail(err, CLP_ERR_OPERAND_ORDER, command, "command %s: required operand '%s' cannot follow optional operand '%s'\n", command->name.data, command_operand->name.data,
                          last_operand->name.data);
    if (d_dyn_array_push_back_ptr(&command->operands, command_operand) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, command, "out of memory\n");

    return CLP_OK;
}

ClpStatus clp_insert_operands(Command *command, const ClpOperandInfo *infos, usize count, ClpError *err)
{
    assert(command != NULL);
    assert(infos != NULL || count == 0);

    for (usize i = 0; i < count; i++)
    {
        const ClpOperandInfo *info    = &infos[i];
        Operand              *operand = NULL;
        ClpStatus             status;

        status = clp_new_operand_raw(&operand, info->name, info->description, info->dest, info->action, info->type, info->required, info->checker, info->transform, err);
        if (status != CLP_OK)
            return status;
        if ((status = clp_add_command_operand(command, operand, err)) != CLP_OK)
        {
            clp_free_operand(operand);
            return status;
        }
    }
    return CLP_OK;
}

ClpStatus clp_try_parse_args(Command *root, char **argv, Command **command, ClpError *err)
{
    assert(root != NULL);
    assert(command != NULL);
    assert(argv != NULL && *argv != NULL);
    if (err)
        *err = (ClpError){0};
    *command = NULL;
    return parse(root, argv, command, err);
}

void clp_parse_args(Command *root, char **argv, Command **command)
{
    ClpError  err;
    ClpStatus status = clp_try_parse_args(root, argv, command, &err);

    if (status == CLP_OK)
        return;
    if (status == CLP_HELP_SHOWN)
        exit(EXIT_SUCCESS);
    clp_print_error(stderr, &err);
    exit(EXIT_FAILURE);
}

ClpStatus clp_list_init(DDynArray *list, ClpError *err)
{
    assert(list != NULL);
    if (d_dyn_array_init(list, sizeof(DStringView), 1, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");
    return CLP_OK;
}

ClpStatus clp_kv_init(DUnorderedMap *map, ClpError *err)
{
    assert(map != NULL);
    if (d_unordered_map_init_not_owned_d_string_view_key(map, sizeof(DStringView), 1, NULL) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");
    return CLP_OK;
}

void clp_list_destroy(DDynArray *list)
{
    d_dyn_array_destroy(list);
}

void clp_kv_destroy(DUnorderedMap *map)
{
    d_unordered_map_destroy(map);
}

void clp_free_option(Option *option)
{
    free_option(option);
}

void clp_free_operand(Operand *operand)
{
    free_operand(operand);
}

void clp_cleanup(Command *root)
{
    assert(root != NULL);
    free_command(root);
}
