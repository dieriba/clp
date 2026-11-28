#include "print.h"

#include "converter.h"
#include "d_general_lib.h"
#include "error.h"
#include "shared.h"

#include <stdio.h>

#define HELP_COL_GAP 4

static usize opt_name_width(clp_option_t *opt)
{
    usize w         = 0;
    bool  has_short = opt->short_name != (char) FLAG_SHORT_OPT_NOT_SET;
    bool  has_long  = opt->long_name.size > 0;

    if (has_short)
        w += 2; /* -X */
    if (has_short && has_long)
        w += 2; /* ", " */
    else if (!has_short && has_long)
        w += 4; /* leading spaces to align with "-X, " */
    if (has_long)
        w += 2 + opt->long_name.size; /* --name */
    if (opt->has_args)
        w += 2 + strlen(type_to_str(opt->type)); /* <type> */
    return w;
}

static usize sub_cmd_name_width(clp_command_t *cmd)
{
    return cmd->name.size;
}

static usize operand_name_width(clp_operand_t *op)
{
    return op->name.size + 2; /* <name> */
}

static usize max_col_width(clp_command_t *root)
{
    usize max = 0;

    d_dyn_array_t *opts = &root->options;
    usize          size = d_dyn_array_get_size(opts);
    for (usize i = 0; i < size; i++)
    {
        clp_option_t *opt = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        usize         w   = opt_name_width(opt);
        if (w > max)
            max = w;
    }

    d_dyn_array_t *cmds = &root->sub_commands;
    size                = d_dyn_array_get_size(cmds);
    for (usize i = 0; i < size; i++)
    {
        clp_command_t *sub = d_dyn_array_get_elem_deref_addr_at_safe(cmds, i);
        usize          w   = sub_cmd_name_width(sub);
        if (w > max)
            max = w;
    }

    d_dyn_array_t *ops = &root->operands;
    size               = d_dyn_array_get_size(ops);
    for (usize i = 0; i < size; i++)
    {
        clp_operand_t *op = d_dyn_array_get_elem_deref_addr_at_safe(ops, i);
        usize          w  = operand_name_width(op);
        if (w > max)
            max = w;
    }

    return max;
}

static void print_options(d_dyn_array_t *opts, usize col_width)
{
    usize n = d_dyn_array_get_size(opts);
    for (usize i = 0; i < n; i++)
    {
        clp_option_t *opt       = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        bool          has_short = opt->short_name != (char) FLAG_SHORT_OPT_NOT_SET;
        bool          has_long  = opt->long_name.size > 0;
        usize         written   = 0;

        printf("  ");
        if (has_short)
        {
            printf("-%c", opt->short_name);
            written += 2;
            if (has_long)
            {
                printf(", ");
                written += 2;
            }
        }
        else if (has_long)
        {
            printf("    ");
            written += 4;
        }
        if (has_long)
        {
            printf("--%.*s", (int) opt->long_name.size, opt->long_name.data);
            written += 2 + opt->long_name.size;
        }
        if (opt->has_args)
        {
            const char *ts = type_to_str(opt->type);
            printf(" <%s>", ts);
            written += 2 + strlen(ts);
        }
        printf("%*s%s\n", (int) (col_width - written + HELP_COL_GAP), "", opt->description);
    }
}

static void print_sub_commands(d_dyn_array_t *cmds, usize col_width)
{
    usize n = d_dyn_array_get_size(cmds);
    for (usize i = 0; i < n; i++)
    {
        clp_command_t *sub = d_dyn_array_get_elem_deref_addr_at_safe(cmds, i);
        printf("  %.*s%*s%s\n", (int) sub->name.size, sub->name.data, (int) (col_width - sub->name.size + HELP_COL_GAP), "", sub->description);
    }
}

static void print_operands(d_dyn_array_t *ops, usize col_width)
{
    usize n = d_dyn_array_get_size(ops);
    for (usize i = 0; i < n; i++)
    {
        clp_operand_t *op     = d_dyn_array_get_elem_deref_addr_at_safe(ops, i);
        usize          name_w = op->name.size + 2;
        printf("  <%.*s>%*s%s\n", (int) op->name.size, op->name.data, (int) (col_width - name_w + HELP_COL_GAP), "", op->description);
    }
}

static void print_command_name_from_root_to_current(FILE *stream, char separator, clp_command_t *current)
{
    if (current->parent_command)
        print_command_name_from_root_to_current(stream, separator, current->parent_command);
    fprintf(stream, "%s%c", current->name.data, separator);
}

static bool applies_to(clp_command_t *command, clp_command_t *level, clp_option_t *opt)
{
    return level == command || opt->global;
}

void print_usage_line(FILE *stream, clp_command_t *command)
{
    fprintf(stream, "Usage: ");
    print_command_name_from_root_to_current(stream, ' ', command);
    fprintf(stream, "[OPTIONS] ");

    for (clp_command_t *level = command; level != NULL; level = level->parent_command)
    {
        d_dyn_array_t *options = &level->options;
        for (size_t i = 0; i < d_dyn_array_get_size(options); i++)
        {
            clp_option_t *option = d_dyn_array_get_elem_deref_addr_at_safe(options, i);
            if (option->required == false || applies_to(command, level, option) == false)
                continue;
            if (option->long_name.size != 0)
                fprintf(stream, "--%s", option->long_name.data);
            else
                fprintf(stream, "-%c", option->short_name);
            fprintf(stream, " <%s> ", type_to_str(option->type));
        }
    }

    bool has_cmd      = d_dyn_array_get_size(&(command->sub_commands)) > 0;
    bool has_operands = d_dyn_array_get_size(&(command->operands)) > 0;
    if (has_cmd)
        fprintf(stream, "<COMMAND>");
    else if (has_operands)
    {
        d_dyn_array_t *operands = &command->operands;
        for (size_t i = 0; i < d_dyn_array_get_size(&(command->operands)); i++)
        {
            clp_operand_t *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
            if (operand->required == false)
                continue;
            fprintf(stream, "<%s", operand->name.data);
            if (operand->action == CLP_OPERAND_ACT_LIST)
                fprintf(stream, " ...");
            fprintf(stream, ">");
            fprintf(stream, " ");
        }
    }

    fprintf(stream, "\n\n");
}

static void print_command_help(clp_command_t *root)
{
    usize col = max_col_width(root);

    printf("Description: %s\n\n", root->description);

    bool has_opts = d_dyn_array_get_size(&root->options) > 0;
    bool has_cmds = d_dyn_array_get_size(&root->sub_commands) > 0;
    bool has_ops  = d_dyn_array_get_size(&root->operands) > 0;

    if (has_opts)
    {
        printf("Options:\n");
        print_options(&root->options, col);
        printf("\n");
    }
    if (has_cmds)
    {
        printf("Commands:\n");
        print_sub_commands(&root->sub_commands, col);
        printf("\n");
    }
    if (has_ops)
    {
        printf("Arguments:\n");
        print_operands(&root->operands, col);
        printf("\n");
    }
}

void clp_print_help(clp_command_t *root)
{
    print_usage_line(stdout, root);
    print_command_help(root);
}

clp_status_t check_help_or_invalid_opt(clp_t *clp, clp_command_t *root, clp_option_t *opt, char *prefix, const char *name, usize len, clp_error_t *err)
{
    (void) clp;
    bool is_long_help  = len == sizeof(HELP_OPT) - 1 && PSEUDO_FAST_STRCMP(name, HELP_OPT);
    bool is_short_help = opt == NULL && prefix[1] == 0 && name[0] == HELP_OPT[0]; // prefix[1] == 0 means short option; only when user has not defined their own -h
    if (is_long_help || is_short_help)
    {
        clp_print_help(root);
        return error_fail(err, CLP_HELP_SHOWN, root, "%s", "");
    }
    if (opt == NULL)
        return error_fail(err, CLP_ERR_UNKNOWN_OPTION, root, "command %s: unknown option '%s%.*s'\n", root->name.data, prefix, (int) len, name);
    return CLP_OK;
}

static bool has_missing_option(clp_command_t *root)
{
    for (clp_command_t *level = root; level != NULL; level = level->parent_command)
    {
        d_dyn_array_t *opts = &level->options;
        for (usize i = 0; i < d_dyn_array_get_size(opts); i++)
        {
            clp_option_t *opt = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
            if (applies_to(root, level, opt) && opt->required == true && opt->value_set == false)
                return true;
        }
    }
    return false;
}

static bool has_missing_operand(clp_command_t *root)
{
    d_dyn_array_t *ops = &root->operands;
    for (usize i = 0; i < d_dyn_array_get_size(ops); i++)
    {
        clp_operand_t *op = d_dyn_array_get_elem_deref_addr_at_safe(ops, i);
        if (op->required == true && op->value_set == false)
            return true;
    }
    return false;
}

clp_status_t check_required_args(clp_command_t *root, clp_error_t *err)
{
    bool missing_opt     = has_missing_option(root);
    bool missing_operand = has_missing_operand(root);

    if (missing_opt == false && missing_operand == false)
        return CLP_OK;

    error_fail(err, CLP_ERR_MISSING_REQUIRED, root, "%s", "");

    if (missing_opt)
    {
        error_append(err, "command %s: the following options were not provided:\n", root->name.data);
        for (clp_command_t *level = root; level != NULL; level = level->parent_command)
        {
            d_dyn_array_t *opts = &level->options;
            for (usize i = 0; i < d_dyn_array_get_size(opts); i++)
            {
                clp_option_t *opt = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
                if (applies_to(root, level, opt) == false || opt->required == false || opt->value_set == true)
                    continue;
                if (opt->long_name.size != 0)
                    error_append(err, "  --%s\n", opt->long_name.data);
                else
                    error_append(err, "  -%c\n", opt->short_name);
            }
        }
        error_append(err, "\n");
    }

    if (missing_operand)
    {
        error_append(err, "command %s: the following operands were not provided:\n", root->name.data);
        d_dyn_array_t *ops = &root->operands;
        for (usize i = 0; i < d_dyn_array_get_size(ops); i++)
        {
            clp_operand_t *op = d_dyn_array_get_elem_deref_addr_at_safe(ops, i);
            if (op->required == true && op->value_set == false)
                error_append(err, "  <%s>\n", op->name.data);
        }
        error_append(err, "\n");
    }

    return CLP_ERR_MISSING_REQUIRED;
}
