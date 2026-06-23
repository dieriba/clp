#include "parse.h"

#include "converter.h"
#include "print.h"
#include "shared.h"

#include <ctype.h>

#define ALPHABET_LOWERCASE "abcdefghijklmnopqrstuvwxyz"
#define ALPHABET_UPPERCASE "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define NUMERIC "0123456789"
#define UNDERSCORE "_"

static bool is_valid_long(DStringView lng)
{
    if (lng.size == 0 || lng.data[0] == '-' || lng.data[0] == '_' ||
        d_string_view_find_first_char_not_in_set_from_start(lng, D_STRING_VIEW_FROM_LITERAL(ALPHABET_LOWERCASE ALPHABET_UPPERCASE HYPHEN UNDERSCORE NUMERIC)) != MAX_SIZE_T_VALUE)
        return false;
    return true;
}

void exit_if_not_valid_long_opt_name(DStringView long_opt)
{
    if (is_valid_long(long_opt) == false)
    {
        clp_eprint("'--%s' is not a valid option name\n", long_opt.data);
        clp_eprint_exit("a long option must start with a letter and contain only [a-z A-Z 0-9 -]\n");
    }
}

static void add_new_kv_pairs(Command *root, DUnorderedMap *kv, const char *kv_pairs, const char *prefix, const char *arg_name, int arg_len)
{
    DStringView list = d_string_view_from_c_string(kv_pairs);
    usize       i = 0, j = 0;
    while (1)
    {
        j                  = d_string_view_find_first_matching_char_from_index(list, ',', i);
        DStringView sub    = d_string_view_subview(list, i, j - i);
        usize       eq_pos = d_string_view_find_first_matching_char_from_index(sub, '=', 0);

        if (eq_pos == MAX_SIZE_T_VALUE)
            clp_eprint_exit("command %s: '%s%.*s': '%.*s' is not a valid key=value pair (missing '=')\n", root->name.data, prefix, arg_len, arg_name, (int) sub.size, sub.data);
        if (eq_pos == 0)
            clp_eprint_exit("command %s: '%s%.*s': '%.*s' has an empty key (expected 'key=value')\n", root->name.data, prefix, arg_len, arg_name, (int) sub.size, sub.data);

        DStringView key = d_string_view_subview(sub, 0, eq_pos);

        if (eq_pos + 1 >= sub.size)
            clp_eprint_exit("command %s: '%s%.*s': key '%.*s' has an empty value (expected 'key=value')\n", root->name.data, prefix, arg_len, arg_name, (int) key.size, key.data);

        DStringView value_str = d_string_view_subview(sub, eq_pos + 1, MAX_SIZE_T_VALUE);
        DResult     result;
        if ((result = d_unordered_map_insert(kv, &key, &value_str)) != D_OK)
            clp_eprint_exit("command %s: '%s%.*s': failed to store key '%.*s': %s\n", root->name.data, prefix, arg_len, arg_name, (int) key.size, key.data, d_error_print_result_as_str(result));
        if (j == MAX_SIZE_T_VALUE)
            break;
        i = j + 1;
    }
}

static void set_opt_value(Command *root, Option *opt, const char *operand, char *prefix, const char *opt_name, usize len_name)
{
    ConversionFn conversion_fn = type_to_conversion_fn(opt->type);

    switch (opt->action)
    {
    case OPT_ACT_SET:
        if (opt->type != TYPE_BOOL)
        {
            if (operand == NULL)
                clp_eprint_exit("command %s: '%s%.*s' option require an argument", root->name.data, prefix, (int) len_name, opt_name);
            char *err_msg = conversion_fn(operand, &opt->value);
            if (err_msg)
                clp_eprint_exit("command %s: invalid value '%s' for '%s%.*s': %s", root->name.data, operand, prefix, (int) len_name, opt_name, err_msg);
        }
        else
            opt->value.value_bool = true;
        break;
    case OPT_ACT_COUNT:
        opt->value.value_usize++;
        break;
    case OPT_ACT_LIST:
        DStringView list = d_string_view_from_c_string(operand);
        usize       i = 0, j = 0;
        while (1)
        {
            j               = d_string_view_find_first_matching_char_from_index(list, ',', i);
            DStringView sub = d_string_view_subview(list, i, j - i);
            DResult     result;
            if ((result = d_dyn_array_push_back(&opt->value.value_list, &sub)) != D_OK)
                clp_eprint_exit("error: %s", d_error_print_result_as_str(result));
            if (j == MAX_SIZE_T_VALUE)
                break;
            i = j + 1;
        }
        break;
    case OPT_ACT_KV:
        add_new_kv_pairs(root, &opt->value.value_kv, operand, prefix, opt_name, (int) len_name);
        break;
    default:
        break;
    }
    if (opt->value_set == false)
        opt->value_set = true;
}

static char **set_opnd_value(Command *root, usize cursor, char *raw_operand, char **argv, bool consume_all)
{
    DDynArray *operands      = &root->operands;
    usize      operands_size = d_dyn_array_get_size(operands);

    if (cursor >= operands_size && raw_operand != NULL)
        clp_eprint_exit("command %s: too many operands provided\n", root->name.data);
    else if (raw_operand == NULL)
        return argv;

    Operand     *operand       = d_dyn_array_get_elem_deref_addr_at_safe(operands, cursor);
    ConversionFn conversion_fn = type_to_conversion_fn(operand->type);

    switch (operand->action)
    {
    case OPND_ACT_SET:
        char *err_msg = conversion_fn(raw_operand, &operand->value);
        if (err_msg)
            clp_eprint_exit("command %s: invalid value '%s' for '<%s>': %s", root->name.data, raw_operand, operand->name.data, err_msg);
        argv++;
        break;
    case OPND_ACT_LIST:
        while (1)
        {
            DStringView list = d_string_view_from_c_string(raw_operand);
            DResult     result;
            if ((result = d_dyn_array_push_back(&operand->value.value_list, &list)) != D_OK)
                clp_eprint_exit("%s", d_error_print_result_as_str(result));
            raw_operand = *(++argv);
            if (raw_operand == NULL || (!consume_all && STR_STARTS_WITH_HYPEN(raw_operand)))
                break;
        }
        break;
    case OPND_ACT_KV:
        add_new_kv_pairs(root, &operand->value.value_kv, raw_operand, "", operand->name.data, (int) operand->name.size);
        argv++;
        break;
    default:
        break;
    }
    if (operand->value_set == false)
        operand->value_set = true;
    return argv;
}

static char **parse_remaining_operands(Command *root, usize *opnd_cursor, char **argv)
{
    usize cursor = *opnd_cursor;
    while (1)
    {
        argv = set_opnd_value(root, cursor, *argv, argv, true);
        if (*argv == NULL)
            break;
        cursor++;
    }
    *opnd_cursor = cursor;
    return argv;
}

static char **parse_long_opt(Command *root, char *lng_opt, char **argv)
{
    DStringView long_opt         = d_string_view_from_c_string(lng_opt);
    usize       eq_pos           = d_string_view_find_first_matching_char_from_start(long_opt, '=');
    bool        has_inline_value = eq_pos != MAX_SIZE_T_VALUE;
    if (has_inline_value)
        long_opt = d_string_view_subview(long_opt, 0, eq_pos);
    exit_if_not_valid_long_opt_name(long_opt);
    Option *opt = clp_get_option_by_long(root, long_opt);
    exit_help_or_if_invalid_opt(root, opt, DOUBLE_HYPHEN, long_opt.data, long_opt.size);
    if (has_inline_value && opt->has_args == false)
        clp_eprint_exit("command %s: option '--%s' doesn't allow an argument\n", root->name.data, opt->long_name.data);
    const char *operand = has_inline_value ? &long_opt.data[eq_pos + 1] : *argv;
    set_opt_value(root, opt, operand, DOUBLE_HYPHEN, long_opt.data, long_opt.size);
    return argv + (*argv != NULL && has_inline_value == false && opt->has_args == true);
}

static char **parse_short_opts(Command *root, char *short_opt, char **argv)
{
    usize i = 0;
    do
    {
        Option *opt = clp_get_option_by_short(root, short_opt[i]);
        exit_help_or_if_invalid_opt(root, opt, HYPHEN, &short_opt[i], 1);
        if (opt->has_args == true)
        {
            bool  consume_next_argv = short_opt[i + 1] == 0;
            char *operand           = consume_next_argv == true ? *argv : &short_opt[i + 1];
            set_opt_value(root, opt, operand, HYPHEN, &short_opt[i], 1);
            return argv + (*argv != NULL && consume_next_argv == true);
        }
        set_opt_value(root, opt, NULL, HYPHEN, &short_opt[i], 1);
    } while (short_opt[++i] != 0);
    return argv;
}

static char **interpret_hyphen(Command *root, char *arg, char **argv, usize *opnd_cusor)
{
    ++argv;
    if (STR_STARTS_WITH_HYPEN(arg))
    {
        if (arg[1] == 0)
            argv = parse_remaining_operands(root, opnd_cusor, argv);
        else
            argv = parse_long_opt(root, &arg[1], argv);
    }
    else
        argv = parse_short_opts(root, arg, argv);
    return argv;
}

static void command_collect_parent_commands_options(Command *command)
{
    DDynArray *opts = &command->options;

    while ((command = command->parent_command) != NULL)
    {
        DDynArray *curr_opts = &command->options;
        for (size_t i = 0; i < d_dyn_array_get_size(curr_opts); i++)
        {
            Option *opt = d_dyn_array_get_elem_deref_addr_at_safe(curr_opts, i);
            if (opt->global && d_dyn_array_push_back_ptr(opts, opt) != D_OK)
                clp_exit_out_of_memory();
        }
    }
}

static void exit_print_command_required_args_if_miss_required_args(Command *root)
{
    if (print_command_required_args_if_miss_required_args(root) == true)
        exit(EXIT_FAILURE);
}

static void exit_print_usage_if_misused_command(Command *command)
{
    if (print_command_required_args_if_miss_required_args(command) == false)
        return;
    print_usage_line(stderr, command);
    fprintf(stderr, "For more information, try '--help'.\n");
    exit(EXIT_FAILURE);
}

void parse(Command *root, char **argv, Command **command)
{
    ++argv; // skip program name at first call then skip current command name already parsed
    char      *s;
    DDynArray *sub_commands       = &root->sub_commands;
    usize      sub_commands_size  = d_dyn_array_get_size(sub_commands);
    usize      cmd_parsed_operand = 0;

    if (*argv == NULL)
    {
        exit_print_usage_if_misused_command(root);
        return;
    }

    while ((s = *argv) != NULL)
    {
        if (STR_STARTS_WITH_HYPEN(s))
        {
            argv = interpret_hyphen(root, &s[1], argv, &cmd_parsed_operand);
            continue;
        }

        for (usize i = 0; i < sub_commands_size; i++)
        {
            Command *sub_cmd = d_dyn_array_get_elem_deref_addr_at_safe(sub_commands, i);
            if (d_string_view_compare_against_c_string(sub_cmd->name, s))
            {
                *command = sub_cmd;
                parse(sub_cmd, argv, command);
                return;
            }
        }

        goto PARSE_OPERAND;
    }

    while ((s = *argv) != NULL)
    {
        if (STR_STARTS_WITH_HYPEN(s))
        {
            argv = interpret_hyphen(root, &s[1], argv, &cmd_parsed_operand);
            continue;
        }
    PARSE_OPERAND:
        argv = set_opnd_value(root, cmd_parsed_operand++, s, argv, false);
    }

    command_collect_parent_commands_options(root);
    exit_print_command_required_args_if_miss_required_args(root);
}