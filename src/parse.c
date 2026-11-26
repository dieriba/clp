#include "parse.h"

#include "converter.h"
#include "error.h"
#include "print.h"
#include "shared.h"

#define ALPHABET_LOWERCASE "abcdefghijklmnopqrstuvwxyz"
#define ALPHABET_UPPERCASE "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define NUMERIC "0123456789"
#define UNDERSCORE "_"

bool is_valid_long(DStringView lng)
{
    if (lng.size == 0 || lng.data[0] == '-' || lng.data[0] == '_' ||
        d_string_view_find_first_char_not_in_set_from_start(lng, D_STRING_VIEW_FROM_LITERAL(ALPHABET_LOWERCASE ALPHABET_UPPERCASE HYPHEN UNDERSCORE NUMERIC)) != MAX_SIZE_T_VALUE)
        return false;
    return true;
}

static ClpStatus check_valid_long_opt_name(Command *root, DStringView long_opt, ClpError *err)
{
    if (is_valid_long(long_opt))
        return CLP_OK;
    return error_fail(err, CLP_ERR_INVALID_NAME, root, "'--%.*s' is not a valid option name\na long option must start with a letter and contain only [a-z A-Z 0-9 -]\n", (int) long_opt.size,
                      long_opt.data);
}

static ClpStatus add_new_kv_pairs(Command *root, DUnorderedMap *kv, ClpChecker checker, const char *kv_pairs, const char *prefix, const char *arg_name, int arg_len, ClpError *err)
{
    DStringView list = d_string_view_from_c_string(kv_pairs);
    usize       i = 0, j = 0;
    while (1)
    {
        j                  = d_string_view_find_first_matching_char_from_index(list, ',', i);
        DStringView sub    = d_string_view_subview(list, i, j - i);
        usize       eq_pos = d_string_view_find_first_matching_char_from_index(sub, '=', 0);

        if (eq_pos == MAX_SIZE_T_VALUE)
            return error_fail(err, CLP_ERR_BAD_KV, root, "command %s: '%s%.*s': '%.*s' is not a valid key=value pair (missing '=')\n", root->name.data, prefix, arg_len, arg_name, (int) sub.size,
                              sub.data);
        if (eq_pos == 0)
            return error_fail(err, CLP_ERR_BAD_KV, root, "command %s: '%s%.*s': '%.*s' has an empty key (expected 'key=value')\n", root->name.data, prefix, arg_len, arg_name, (int) sub.size,
                              sub.data);

        DStringView key = d_string_view_subview(sub, 0, eq_pos);

        if (eq_pos + 1 >= sub.size)
            return error_fail(err, CLP_ERR_BAD_KV, root, "command %s: '%s%.*s': key '%.*s' has an empty value (expected 'key=value')\n", root->name.data, prefix, arg_len, arg_name, (int) key.size,
                              key.data);

        DStringView value_str = d_string_view_subview(sub, eq_pos + 1, MAX_SIZE_T_VALUE);
        DResult     result;
        const char *err_msg;
        if (checker && (err_msg = checker(&(Value) {.value_key_value = {.key = key, .value = value_str}})) != NULL)
            return error_fail(err, CLP_ERR_BAD_VALUE, root, "command %s: invalid value '%.*s' for '%s%.*s': %s\n", root->name.data, (int) sub.size, sub.data, prefix, arg_len, arg_name, err_msg);
        if ((result = d_unordered_map_insert(kv, &key, &value_str)) != D_OK)
            return error_fail(err, CLP_ERR_ALLOC, root, "command %s: '%s%.*s': failed to store key '%.*s': %s\n", root->name.data, prefix, arg_len, arg_name, (int) key.size, key.data,
                              d_error_print_result_as_str(result));
        if (j == MAX_SIZE_T_VALUE)
            break;
        i = j + 1;
    }
    return CLP_OK;
}

static ClpStatus set_opt_value(Command *root, Option *opt, const char *operand, char *prefix, const char *opt_name, usize len_name, ClpError *err)
{
    ConversionFn conversion_fn = type_to_conversion_fn(opt->type);

    switch (opt->action)
    {
    case OPT_ACT_SET:
        if (opt->type != TYPE_BOOL)
        {
            if (operand == NULL)
                return error_fail(err, CLP_ERR_MISSING_ARG, root, "command %s: '%s%.*s' option require an argument\n", root->name.data, prefix, (int) len_name, opt_name);
            const char *err_msg = conversion_fn(operand, &opt->value);
            if (opt->checker && err_msg == NULL)
                err_msg = opt->checker(&opt->value);
            if (err_msg)
                return error_fail(err, CLP_ERR_BAD_VALUE, root, "command %s: invalid value '%s' for '%s%.*s': %s\n", root->name.data, operand, prefix, (int) len_name, opt_name, err_msg);
        }
        else
            opt->value.value_bool = true;
        break;
    case OPT_ACT_COUNT:
        opt->value.value_usize++;
        break;
    case OPT_ACT_LIST:
        if (operand == NULL)
            return error_fail(err, CLP_ERR_MISSING_ARG, root, "command %s: '%s%.*s' option require an argument\n", root->name.data, prefix, (int) len_name, opt_name);
        DStringView list = d_string_view_from_c_string(operand);
        usize       i = 0, j = 0;
        while (1)
        {
            j               = d_string_view_find_first_matching_char_from_index(list, ',', i);
            DStringView sub = d_string_view_subview(list, i, j - i);
            DResult     result;
            const char *err_msg;
            if (opt->checker && (err_msg = opt->checker(&(Value) {.value_d_string_view = sub})) != NULL)
                return error_fail(err, CLP_ERR_BAD_VALUE, root, "command %s: invalid value '%.*s' for '%s%.*s': %s\n", root->name.data, (int) sub.size, sub.data, prefix, (int) len_name, opt_name,
                                  err_msg);
            if ((result = d_dyn_array_push_back(&opt->value.value_list, &sub)) != D_OK)
                return error_fail(err, CLP_ERR_ALLOC, root, "error: %s\n", d_error_print_result_as_str(result));
            if (j == MAX_SIZE_T_VALUE)
                break;
            i = j + 1;
        }
        break;
    case OPT_ACT_KV:
        if (operand == NULL)
            return error_fail(err, CLP_ERR_MISSING_ARG, root, "command %s: '%s%.*s' option require an argument\n", root->name.data, prefix, (int) len_name, opt_name);
        CLP_TRY(add_new_kv_pairs(root, &opt->value.value_kv, opt->checker, operand, prefix, opt_name, (int) len_name, err));
        break;
    default:
        break;
    }
    if (opt->value_set == false)
        opt->value_set = true;
    return CLP_OK;
}

static ClpStatus set_operand_value(Command *root, usize cursor, char *raw_operand, char ***argvp, bool consume_all, ClpError *err)
{
    DDynArray *operands      = &root->operands;
    usize      operands_size = d_dyn_array_get_size(operands);
    char     **argv          = *argvp;

    if (raw_operand == NULL)
        return CLP_OK;
    if (cursor >= operands_size)
        return error_fail(err, CLP_ERR_TOO_MANY_OPERANDS, root, "command %s: too many operands provided\n", root->name.data);

    Operand     *operand       = d_dyn_array_get_elem_deref_addr_at_safe(operands, cursor);
    ConversionFn conversion_fn = type_to_conversion_fn(operand->type);

    switch (operand->action)
    {
    case OPERAND_ACT_SET:
        const char *err_msg = conversion_fn(raw_operand, &operand->value);
        if (operand->checker && err_msg == NULL)
            err_msg = operand->checker(&operand->value);
        if (err_msg)
            return error_fail(err, CLP_ERR_BAD_VALUE, root, "command %s: invalid value '%s' for '<%s>': %s\n", root->name.data, raw_operand, operand->name.data, err_msg);
        argv++;
        break;
    case OPERAND_ACT_LIST:
        while (1)
        {
            DStringView list = d_string_view_from_c_string(raw_operand);
            DResult     result;
            const char *err_msg;
            if (operand->checker && (err_msg = operand->checker(&(Value) {.value_d_string_view = list})) != NULL)
                return error_fail(err, CLP_ERR_BAD_VALUE, root, "command %s: invalid value '%s' for '<%s>': %s\n", root->name.data, raw_operand, operand->name.data, err_msg);
            if ((result = d_dyn_array_push_back(&operand->value.value_list, &list)) != D_OK)
                return error_fail(err, CLP_ERR_ALLOC, root, "%s\n", d_error_print_result_as_str(result));
            raw_operand = *(++argv);
            if (raw_operand == NULL || (!consume_all && STR_STARTS_WITH_HYPEN(raw_operand)))
                break;
        }
        break;
    case OPERAND_ACT_KV:
        CLP_TRY(add_new_kv_pairs(root, &operand->value.value_kv, operand->checker, raw_operand, "", operand->name.data, (int) operand->name.size, err));
        argv++;
        break;
    default:
        break;
    }
    if (operand->value_set == false)
        operand->value_set = true;
    *argvp = argv;
    return CLP_OK;
}

static ClpStatus parse_remaining_operands(Command *root, usize *operand_cursor, char ***argvp, ClpError *err)
{
    usize cursor = *operand_cursor;
    while (**argvp != NULL)
    {
        CLP_TRY(set_operand_value(root, cursor, **argvp, argvp, true, err));
        if (**argvp == NULL)
            break;
        cursor++;
    }
    *operand_cursor = cursor;
    return CLP_OK;
}

static ClpStatus parse_long_opt(Command *root, char *lng_opt, char ***argvp, ClpError *err)
{
    char      **argv             = *argvp;
    DStringView long_opt         = d_string_view_from_c_string(lng_opt);
    usize       eq_pos           = d_string_view_find_first_matching_char_from_start(long_opt, '=');
    bool        has_inline_value = eq_pos != MAX_SIZE_T_VALUE;
    if (has_inline_value)
        long_opt = d_string_view_subview(long_opt, 0, eq_pos);
    CLP_TRY(check_valid_long_opt_name(root, long_opt, err));
    Option *opt = clp_get_option_by_long(root, long_opt);
    CLP_TRY(check_help_or_invalid_opt(root, opt, DOUBLE_HYPHEN, long_opt.data, long_opt.size, err));
    if (has_inline_value && opt->has_args == false)
        return error_fail(err, CLP_ERR_UNEXPECTED_ARG, root, "command %s: option '--%s' doesn't allow an argument\n", root->name.data, opt->long_name.data);
    const char *operand = has_inline_value ? &long_opt.data[eq_pos + 1] : *argv;
    CLP_TRY(set_opt_value(root, opt, operand, DOUBLE_HYPHEN, long_opt.data, long_opt.size, err));
    *argvp = argv + (*argv != NULL && has_inline_value == false && opt->has_args == true);
    return CLP_OK;
}

static ClpStatus parse_short_opts(Command *root, char *short_opt, char ***argvp, ClpError *err)
{
    char **argv = *argvp;
    usize  i    = 0;
    do
    {
        Option *opt = clp_get_option_by_short(root, short_opt[i]);
        CLP_TRY(check_help_or_invalid_opt(root, opt, HYPHEN, &short_opt[i], 1, err));
        if (opt->has_args == true)
        {
            bool  consume_next_argv = short_opt[i + 1] == 0;
            char *operand           = consume_next_argv == true ? *argv : &short_opt[i + 1];
            CLP_TRY(set_opt_value(root, opt, operand, HYPHEN, &short_opt[i], 1, err));
            *argvp = argv + (*argv != NULL && consume_next_argv == true);
            return CLP_OK;
        }
        CLP_TRY(set_opt_value(root, opt, NULL, HYPHEN, &short_opt[i], 1, err));
    } while (short_opt[++i] != 0);
    return CLP_OK;
}

static ClpStatus interpret_hyphen(Command *root, char *arg, char ***argvp, usize *operand_cusor, ClpError *err)
{
    ++*argvp;
    if (STR_STARTS_WITH_HYPEN(arg))
    {
        if (arg[1] == 0)
            return parse_remaining_operands(root, operand_cusor, argvp, err);
        return parse_long_opt(root, &arg[1], argvp, err);
    }
    return parse_short_opts(root, arg, argvp, err);
}

static ClpStatus command_collect_parent_commands_options(Command *command, ClpError *err)
{
    Command   *root = command;
    DDynArray *opts = &command->options;

    while ((command = command->parent_command) != NULL)
    {
        DDynArray *curr_opts = &command->options;
        for (size_t i = 0; i < d_dyn_array_get_size(curr_opts); i++)
        {
            Option *opt = d_dyn_array_get_elem_deref_addr_at_safe(curr_opts, i);
            if (opt->global && d_dyn_array_push_back_ptr(opts, opt) != D_OK)
                return error_fail(err, CLP_ERR_ALLOC, root, "out of memory\n");
        }
    }
    return CLP_OK;
}

ClpStatus parse(Command *root, char **argv, Command **command, ClpError *err)
{
    ++argv; // skip program name at first call then skip current command name already parsed
    char      *s;
    DDynArray *sub_commands       = &root->sub_commands;
    usize      sub_commands_size  = d_dyn_array_get_size(sub_commands);
    usize      cmd_parsed_operand = 0;

    if (*argv == NULL)
        return check_required_args(root, err);

    while ((s = *argv) != NULL)
    {
        if (STR_STARTS_WITH_HYPEN(s))
        {
            CLP_TRY(interpret_hyphen(root, &s[1], &argv, &cmd_parsed_operand, err));
            continue;
        }

        for (usize i = 0; i < sub_commands_size; i++)
        {
            Command *sub_cmd = d_dyn_array_get_elem_deref_addr_at_safe(sub_commands, i);
            if (d_string_view_compare_against_c_string(sub_cmd->name, s))
            {
                *command = sub_cmd;
                return parse(sub_cmd, argv, command, err);
            }
        }

        goto PARSE_OPERAND;
    }

    while ((s = *argv) != NULL)
    {
        if (STR_STARTS_WITH_HYPEN(s))
        {
            CLP_TRY(interpret_hyphen(root, &s[1], &argv, &cmd_parsed_operand, err));
            continue;
        }
    PARSE_OPERAND:
        CLP_TRY(set_operand_value(root, cmd_parsed_operand++, s, &argv, false, err));
    }

    CLP_TRY(command_collect_parent_commands_options(root, err));
    return check_required_args(root, err);
}
