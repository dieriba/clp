#include "print.h"

#include "converter.h"
#include "shared.h"

#include <stdio.h>

#define HELP_COL_GAP 4

static usize opt_name_width(Option *opt)
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

static usize sub_cmd_name_width(Command *cmd)
{
    return cmd->name.size;
}

static usize operand_name_width(Operand *op)
{
    return op->name.size + 2; /* <name> */
}

static usize max_col_width(Command *root)
{
    usize max = 0;

    DDynArray *opts = &root->options;
    usize      size = d_dyn_array_get_size(opts);
    for (usize i = 0; i < size; i++)
    {
        Option *opt = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        usize   w   = opt_name_width(opt);
        if (w > max)
            max = w;
    }

    DDynArray *cmds = &root->sub_commands;
    size            = d_dyn_array_get_size(cmds);
    for (usize i = 0; i < size; i++)
    {
        Command *sub = d_dyn_array_get_elem_deref_addr_at_safe(cmds, i);
        usize    w   = sub_cmd_name_width(sub);
        if (w > max)
            max = w;
    }

    DDynArray *ops = &root->operands;
    size           = d_dyn_array_get_size(ops);
    for (usize i = 0; i < size; i++)
    {
        Operand *op = d_dyn_array_get_elem_deref_addr_at_safe(ops, i);
        usize    w  = operand_name_width(op);
        if (w > max)
            max = w;
    }

    return max;
}

static void print_options(DDynArray *opts, usize col_width)
{
    usize n = d_dyn_array_get_size(opts);
    for (usize i = 0; i < n; i++)
    {
        Option *opt       = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        bool    has_short = opt->short_name != (char) FLAG_SHORT_OPT_NOT_SET;
        bool    has_long  = opt->long_name.size > 0;
        usize   written   = 0;

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

static void print_sub_commands(DDynArray *cmds, usize col_width)
{
    usize n = d_dyn_array_get_size(cmds);
    for (usize i = 0; i < n; i++)
    {
        Command *sub = d_dyn_array_get_elem_deref_addr_at_safe(cmds, i);
        printf("  %.*s%*s%s\n", (int) sub->name.size, sub->name.data, (int) (col_width - sub->name.size + HELP_COL_GAP), "", sub->description);
    }
}

static void print_operands(DDynArray *ops, usize col_width)
{
    usize n = d_dyn_array_get_size(ops);
    for (usize i = 0; i < n; i++)
    {
        Operand *op     = d_dyn_array_get_elem_deref_addr_at_safe(ops, i);
        usize    name_w = op->name.size + 2;
        printf("  <%.*s>%*s%s\n", (int) op->name.size, op->name.data, (int) (col_width - name_w + HELP_COL_GAP), "", op->description);
    }
}

static void print_command_name_from_root_to_current(FILE *stream, char separator, Command *current)
{
    if (current->parent_command)
        print_command_name_from_root_to_current(stream, separator, current->parent_command);
    fprintf(stream, "%s%c", current->name.data, separator);
}

void print_usage_line(FILE *stream, Command *command)
{
    fprintf(stream, "Usage: ");
    print_command_name_from_root_to_current(stream, ' ', command);
    fprintf(stream, "[OPTIONS] ");

    DDynArray *options = &command->options;
    for (size_t i = 0; i < d_dyn_array_get_size(&(command->options)); i++)
    {
        Option *option = d_dyn_array_get_elem_deref_addr_at_safe(options, i);
        if (option->required == false)
            continue;
        if (option->long_name.size != 0)
            fprintf(stream, "--%s", option->long_name.data);
        else
            fprintf(stream, "-%c", option->short_name);
        fprintf(stream, " <%s> ", type_to_str(option->type));
    }

    bool has_cmd      = d_dyn_array_get_size(&(command->sub_commands)) > 0;
    bool has_operands = d_dyn_array_get_size(&(command->operands)) > 0;
    if (has_cmd)
        fprintf(stream, "<COMMAND>");
    else if (has_operands)
    {
        DDynArray *operands = &command->operands;
        for (size_t i = 0; i < d_dyn_array_get_size(&(command->operands)); i++)
        {
            Operand *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
            if (operand->required == false)
                continue;
            fprintf(stream, "<%s>", operand->name.data);
            if (operand->action == OPERAND_ACT_LIST)
                fprintf(stream, "...");
            fprintf(stream, " ");
        }
    }

    fprintf(stream, "\n\n");
}

static bool print_command_required_opts_if_missing(Command *root)
{
    DDynArray *opts                  = &root->options;
    usize      size                  = d_dyn_array_get_size(opts);
    bool       required_opts_not_set = false;
    for (size_t i = 0; i < size; i++)
    {
        Option *opt = d_dyn_array_get_elem_deref_addr_at_safe(opts, i);
        if (opt->required == true && opt->value_set == false)
        {
            if (required_opts_not_set == false)
            {
                clp_eprint("command %s: the following options were not provided:\n", root->name.data);
                required_opts_not_set = true;
            }
            if (opt->long_name.size != 0)
                clp_eprint("--%s\n", opt->long_name.data);
            else
                clp_eprint("-%c\n", opt->short_name);
        }
    }
    return required_opts_not_set;
}

static bool print_command_required_operands_if_missing(Command *root)
{
    DDynArray *operands                  = &root->operands;
    usize      size                      = d_dyn_array_get_size(operands);
    bool       required_operands_not_set = false;
    for (size_t i = 0; i < size; i++)
    {
        Operand *operand = d_dyn_array_get_elem_deref_addr_at_safe(operands, i);
        if (operand->required == true && operand->value_set == false)
        {
            if (required_operands_not_set == false)
            {
                clp_eprint("command %s: the following operands were not provided:\n", root->name.data);
                required_operands_not_set = true;
            }
            clp_eprint("<%s>\n", operand->name.data);
        }
    }
    return required_operands_not_set;
}

static void print_command_help(Command *root)
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

static void exit_print_help(Command *root)
{
    print_usage_line(stdout, root);
    print_command_help(root);
    exit(EXIT_SUCCESS);
}

void exit_help_or_if_invalid_opt(Command *root, Option *opt, char *prefix, const char *name, usize len)
{
    if (len == sizeof(HELP_OPT) - 1 && PSEUDO_FAST_STRCMP(name, HELP_OPT))
        exit_print_help(root);

    if (opt == NULL)
    {
        if (prefix[1] == 0 && name[0] == HELP_OPT[0]) // if prefix[1] == 0 then we know it is a short option
            exit_print_help(root);                    // printing command help only if user has not define a short option with 'h'
        clp_eprint_exit("command %s: unknown option '%s%.*s'\n", root->name.data, prefix, (int) len, name);
    }
}

bool print_command_required_args_if_miss_required_args(Command *root)
{
    bool miss = false;
    if (print_command_required_opts_if_missing(root) == true)
    {
        miss = true;
        fprintf(stderr, "\n");
    }

    if (print_command_required_operands_if_missing(root) == true)
    {
        miss = true;
        fprintf(stderr, "\n");
    }

    return miss;
}
