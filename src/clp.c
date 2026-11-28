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

clp_status_t clp_new_command(clp_command_t **out, int code, char *name, char *description, clp_validator_t validator, void *ctx, clp_error_t *err)
{
    assert(out != NULL);
    assert(name != NULL);

    *out                   = NULL;
    clp_command_t *command = malloc(sizeof(clp_command_t));
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

static const char *check_transform_combination(clp_type_t type, bool is_set, clp_checker_t checker, clp_transform_t transform)
{
    if (transform != NULL && type != CLP_TYPE_CUSTOM)
        return "a transform requires type 'custom'";
    if (transform == NULL && type == CLP_TYPE_CUSTOM)
        return "type 'custom' requires a transform";
    if (transform != NULL && is_set == false)
        return "a transform is only allowed with the 'set' action";
    if (transform != NULL && checker != NULL)
        return "a transform cannot be combined with a checker";
    return NULL;
}

clp_status_t clp_new_option_raw(clp_option_t **out, char *long_name, char *short_name, char *description, void *dest, clp_opt_action_t action, clp_type_t type, bool required, bool global,
                                clp_checker_t checker, clp_transform_t transform, clp_error_t *err)
{
    assert(out != NULL);
    assert(long_name != NULL || short_name != NULL);
    assert(dest != NULL || type == CLP_TYPE_CUSTOM);

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

    if (action == CLP_OPT_ACT_COUNT && type != CLP_TYPE_USIZE)
    {
        if (long_name)
            return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '--%s' has action 'count' but type '%s' is not valid\ncount action requires an integer type [u8 u16 u32 u64]\n", long_name,
                              type_to_str(type));
        return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '-%c' has action 'count' but type '%s' is not valid\ncount action requires an integer type [u8 u16 u32 u64]\n", shrt_name,
                          type_to_str(type));
    }

    const char *bad_combination = check_transform_combination(type, action == CLP_OPT_ACT_SET, checker, transform);
    if (bad_combination)
    {
        if (long_name)
            return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '--%s': %s\n", long_name, bad_combination);
        return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "option '-%c': %s\n", shrt_name, bad_combination);
    }

    clp_option_t *opt = malloc(sizeof(clp_option_t));
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
    opt->has_args    = !(type == CLP_TYPE_BOOL || action == CLP_OPT_ACT_COUNT);
    *out             = opt;
    return CLP_OK;
}

clp_status_t clp_new_operand_raw(clp_operand_t **out, char *name, char *description, void *dest, clp_operand_action_t action, clp_type_t type, bool required, clp_checker_t checker,
                                 clp_transform_t transform, clp_error_t *err)
{
    assert(out != NULL);
    assert(name != NULL);
    assert(dest != NULL || type == CLP_TYPE_CUSTOM);

    *out = NULL;
    if (is_valid_long(d_string_view_from_c_string(name)) == false)
        return error_fail(err, CLP_ERR_INVALID_NAME, NULL, "'--%s' is not a valid option name\n" LONG_NAME_RULE, name);

    const char *bad_combination = check_transform_combination(type, action == CLP_OPERAND_ACT_SET, checker, transform);
    if (bad_combination)
        return error_fail(err, CLP_ERR_INVALID_TYPE, NULL, "operand '<%s>': %s\n", name, bad_combination);

    clp_operand_t *operand = malloc(sizeof(clp_operand_t));
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

static inline bool eq_short_opt(clp_option_t *opt, void *ctx)
{
    char c = *(char *) ctx;
    return c != (char) FLAG_SHORT_OPT_NOT_SET && opt->short_name == c;
}

static inline bool eq_long_opt(clp_option_t *opt, void *ctx)
{
    return opt->long_name.size > 0 && d_string_view_compare(opt->long_name, *((d_string_view_t *) ctx));
}

static clp_option_t *get_option_by_predicate(clp_command_t *command, void *ctx, bool (*predicate)(clp_option_t *, void *))
{
    d_dyn_array_t *opts = &command->options;
    usize          size = d_dyn_array_get_size(opts);
    for (usize i = 0; i < size; i++)
    {
        clp_option_t *option = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        if (predicate(option, ctx) == true)
            return option;
    }
    return NULL;
}

static clp_operand_t *get_command_operand_by_name(clp_command_t *command, d_string_view_t name)
{
    d_dyn_array_t *operands = &command->operands;
    usize          size     = d_dyn_array_get_size(operands);
    for (usize i = 0; i < size; i++)
    {
        clp_operand_t *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
        if (d_string_view_compare(operand->name, name))
            return operand;
    }
    return NULL;
}

clp_option_t *clp_get_option_by_short(clp_command_t *command, char shrt)
{
    assert(command != NULL);
    return get_option_by_predicate(command, &shrt, eq_short_opt);
}

clp_option_t *clp_get_option_by_long(clp_command_t *command, d_string_view_t lng)
{
    assert(command != NULL);
    return get_option_by_predicate(command, &lng, eq_long_opt);
}

clp_operand_t *clp_get_operand(clp_command_t *command, d_string_view_t operand_name)
{
    assert(command != NULL);
    d_dyn_array_t *operands = &command->operands;
    usize          size     = d_dyn_array_get_size(operands);
    for (usize i = 0; i < size; i++)
    {
        clp_operand_t *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
        if (d_string_view_compare(operand->name, operand_name))
            return operand;
    }
    return NULL;
}

clp_status_t clp_add_command_sub_command(clp_command_t *command, clp_command_t *sub_command, clp_error_t *err)
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

clp_status_t clp_add_command_option(clp_command_t *command, clp_option_t *command_option, clp_error_t *err)
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

clp_status_t clp_insert_opts(clp_command_t *command, const clp_option_info_t *infos, usize count, clp_error_t *err)
{
    assert(command != NULL);
    assert(infos != NULL || count == 0);

    for (usize i = 0; i < count; i++)
    {
        const clp_option_info_t *info = &infos[i];
        clp_option_t            *opt  = NULL;
        clp_status_t             status;

        status =
            clp_new_option_raw(&opt, info->long_name, info->short_name, info->description, info->dest, info->action, info->type, info->required, info->global, info->checker, info->transform, err);
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

clp_status_t clp_add_command_operand(clp_command_t *command, clp_operand_t *command_operand, clp_error_t *err)
{
    assert(command != NULL);
    assert(command_operand != NULL);

    if (d_dyn_array_get_size(&(command->sub_commands)) > 0)
        return error_fail(err, CLP_ERR_CONFLICT, command, "invalid argument: command '%s' cannot have both operands and subcommands\n", command->name.data);

    d_dyn_array_t *ops          = &command->operands;
    usize          size         = d_dyn_array_get_size(ops);
    clp_operand_t *last_operand = size == 0 ? NULL : d_dyn_array_get_elem_deref_addr_at_safe(ops, size - 1);

    if (get_command_operand_by_name(command, command_operand->name))
        clp_print_warning("warning: operand name '%s' already used, consider a more descriptive name\n", command_operand->name.data);
    else if (last_operand != NULL && (last_operand->required == false && command_operand->required == true))
        return error_fail(err, CLP_ERR_OPERAND_ORDER, command, "command %s: required operand '%s' cannot follow optional operand '%s'\n", command->name.data, command_operand->name.data,
                          last_operand->name.data);
    if (d_dyn_array_push_back_ptr(&command->operands, command_operand) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, command, "out of memory\n");

    return CLP_OK;
}

clp_status_t clp_insert_operands(clp_command_t *command, const clp_operand_info_t *infos, usize count, clp_error_t *err)
{
    assert(command != NULL);
    assert(infos != NULL || count == 0);

    for (usize i = 0; i < count; i++)
    {
        const clp_operand_info_t *info    = &infos[i];
        clp_operand_t            *operand = NULL;
        clp_status_t              status;

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

clp_status_t clp_try_parse_args(clp_command_t *root, char **argv, clp_command_t **command, clp_error_t *err)
{
    assert(root != NULL);
    assert(command != NULL);
    assert(argv != NULL && *argv != NULL);
    if (err)
        *err = (clp_error_t){0};
    *command = NULL;
    return parse(root, argv, command, err);
}

void clp_parse_args(clp_command_t *root, char **argv, clp_command_t **command)
{
    clp_error_t  err;
    clp_status_t status = clp_try_parse_args(root, argv, command, &err);

    if (status == CLP_OK)
        return;
    if (status == CLP_HELP_SHOWN)
        exit(EXIT_SUCCESS);
    clp_print_error(stderr, &err);
    exit(EXIT_FAILURE);
}

clp_status_t clp_list_init(d_dyn_array_t *list, clp_error_t *err)
{
    assert(list != NULL);
    if (d_dyn_array_init(list, sizeof(d_string_view_t), 1, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");
    return CLP_OK;
}

clp_status_t clp_kv_init(d_unordered_map_t *map, clp_error_t *err)
{
    assert(map != NULL);
    if (d_unordered_map_init_not_owned_d_string_view_key(map, sizeof(d_string_view_t), 1, NULL) != D_OK)
        return error_fail(err, CLP_ERR_ALLOC, NULL, "out of memory\n");
    return CLP_OK;
}

void clp_list_destroy(d_dyn_array_t *list)
{
    d_dyn_array_destroy(list);
}

void clp_kv_destroy(d_unordered_map_t *map)
{
    d_unordered_map_destroy(map);
}

void clp_free_option(clp_option_t *option)
{
    free_option(option);
}

void clp_free_operand(clp_operand_t *operand)
{
    free_operand(operand);
}

void clp_cleanup(clp_command_t *root)
{
    free_command(root);
}
