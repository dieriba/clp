#include "clp.h"
#include "d_test.h"

#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ── helpers ────────────────────────────────────────────────────────────── */

static Option *init_bool_opt(char *lng, char *sht, bool required)
{
    return clp_new_option(lng, sht, NULL, TYPE_BOOL, required, false);
}

static Option *init_str_opt(char *lng, char *sht, bool required)
{
    return clp_new_option(lng, sht, NULL, TYPE_STR, required, false);
}

static Option *init_count_opt(char *lng, char *sht)
{
    return clp_new_option_count(lng, sht, NULL, false);
}

static Option *init_list_opt(char *lng, char *sht)
{
    return clp_new_option_list(lng, sht, NULL, false, false);
}

static void init_str_operand(Operand *op, char *name, bool required)
{
    clp_init_opnd(op, name, NULL, TYPE_STR, required);
}

static void init_list_operand(Operand *op, char *name, bool required)
{
    clp_init_opnd_list(op, name, NULL, required);
}

static Option *init_kv_opt(char *lng, char *sht)
{
    return clp_new_option_kv(lng, sht, NULL, false, false);
}

static void init_kv_operand(Operand *op, char *name, bool required)
{
    clp_init_opnd_kv(op, name, NULL, required);
}

/* ── fork/pipe helper (used by null-guard and error tests) ──────────────── */

typedef struct
{
    int  status;
    char err[512];
    char out[1024];
} ChildResult;

static ChildResult run_child(void (*fn)(void))
{
    int pfd[2];
    pipe(pfd);
    pid_t pid = fork();
    if (pid == 0)
    {
        close(pfd[0]);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        fn();
        _exit(0);
    }
    close(pfd[1]);
    ChildResult r     = {0};
    ssize_t     total = 0, n;
    while ((n = read(pfd[0], r.err + total, sizeof(r.err) - 1 - total)) > 0)
        total += n;
    r.err[total] = '\0';
    close(pfd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

static ChildResult run_child_stdout(void (*fn)(void))
{
    int pfd[2];
    pipe(pfd);
    pid_t pid = fork();
    if (pid == 0)
    {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        fn();
        _exit(0);
    }
    close(pfd[1]);
    ChildResult r     = {0};
    ssize_t     total = 0, n;
    while ((n = read(pfd[0], r.out + total, sizeof(r.out) - 1 - total)) > 0)
        total += n;
    r.out[total] = '\0';
    close(pfd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

/* ── getter tests ───────────────────────────────────────────────────────── */

static void test_get_option_by_short_finds_registered_option(void)
{
    Command *cmd;
    Option  *opt;
    cmd = clp_new_command(0, "prog", NULL);
    opt = init_bool_opt("verbose", "v", false);
    clp_add_command_option(cmd, opt);
    D_TEST_EXPR(clp_get_option_by_short(cmd, 'v') == opt);
    clp_cleanup(cmd);
}

static void test_get_option_by_short_returns_null_for_unknown(void)
{
    Command *cmd;
    Option  *opt;
    cmd = clp_new_command(0, "prog", NULL);
    opt = init_bool_opt("verbose", "v", false);
    clp_add_command_option(cmd, opt);
    D_TEST_NULL(clp_get_option_by_short(cmd, 'x'));
    clp_cleanup(cmd);
}

static void test_get_option_by_long_finds_registered_option(void)
{
    Command *cmd;
    Option  *opt;
    cmd = clp_new_command(0, "prog", NULL);
    opt = init_bool_opt("verbose", "v", false);
    clp_add_command_option(cmd, opt);
    D_TEST_EXPR(clp_get_option_by_long(cmd, D_STRING_VIEW_FROM_LITERAL("verbose")) == opt);
    clp_cleanup(cmd);
}

static void test_get_option_by_long_returns_null_for_unknown(void)
{
    Command *cmd;
    Option  *opt;
    cmd = clp_new_command(0, "prog", NULL);
    opt = init_bool_opt("verbose", "v", false);
    clp_add_command_option(cmd, opt);
    D_TEST_NULL(clp_get_option_by_long(cmd, D_STRING_VIEW_FROM_LITERAL("quiet")));
    clp_cleanup(cmd);
}

static void test_get_opnd_finds_registered_operand(void)
{
    Command *cmd;
    Operand  op;
    cmd = clp_new_command(0, "prog", NULL);
    init_str_operand(&op, "file", false);
    clp_add_command_operand(cmd, &op);
    D_TEST_EXPR(clp_get_operand(cmd, D_STRING_VIEW_FROM_LITERAL("file")) == &op);
    clp_cleanup(cmd);
}

static void test_get_opnd_returns_null_for_unknown(void)
{
    Command *cmd;
    Operand  op;
    cmd = clp_new_command(0, "prog", NULL);
    init_str_operand(&op, "file", false);
    clp_add_command_operand(cmd, &op);
    D_TEST_NULL(clp_get_operand(cmd, D_STRING_VIEW_FROM_LITERAL("output")));
    clp_cleanup(cmd);
}

/* ── bool option parsing ────────────────────────────────────────────────── */

static void test_long_bool_flag_sets_value(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", "--verbose", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(verbose->value.value_bool == true);
    clp_cleanup(root);
}

static void test_short_bool_flag_sets_value(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", "-v", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(verbose->value.value_bool == true);
    clp_cleanup(root);
}

static void test_unprovided_optional_bool_stays_unset(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == false);
    D_TEST_EXPR(verbose->value.value_bool == false);
    clp_cleanup(root);
}

/* POSIX: multiple bool flags combined in one token: -abc */
static void test_combined_short_bool_flags(void)
{
    Command *root;
    Option  *fa, *fb, *fc;
    root = clp_new_command(0, "prog", NULL);
    fa   = init_bool_opt("aaa", "a", false);
    fb   = init_bool_opt("bbb", "b", false);
    fc   = init_bool_opt("ccc", "c", false);
    clp_add_command_option(root, fa);
    clp_add_command_option(root, fb);
    clp_add_command_option(root, fc);

    char    *argv[] = {"prog", "-abc", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == true);
    D_TEST_EXPR(fc->value_set == true);
    D_TEST_EXPR(fa->value.value_bool == true);
    D_TEST_EXPR(fb->value.value_bool == true);
    D_TEST_EXPR(fc->value.value_bool == true);
    clp_cleanup(root);
}

/* Parsing multiple separate bool flags */
static void test_multiple_separate_short_bool_flags(void)
{
    Command *root;
    Option  *fa, *fb;
    root = clp_new_command(0, "prog", NULL);
    fa   = init_bool_opt("aaa", "a", false);
    fb   = init_bool_opt("bbb", "b", false);
    clp_add_command_option(root, fa);
    clp_add_command_option(root, fb);

    char    *argv[] = {"prog", "-a", "-b", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == true);
    clp_cleanup(root);
}

/* Only first flag in combined token is set when second is unknown */
static void test_combined_flags_only_set_found_options(void)
{
    Command *root;
    Option  *fa, *fb;
    root = clp_new_command(0, "prog", NULL);
    fa   = init_bool_opt("aaa", "a", false);
    fb   = init_bool_opt("bbb", "b", false);
    clp_add_command_option(root, fa);
    clp_add_command_option(root, fb);

    char    *argv[] = {"prog", "-a", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == false);
    clp_cleanup(root);
}

/* ── non-bool options: value_set and value correctness ─────────────────── */

/* POSIX: -oVALUE (value is remainder of token, no space) */
static void test_short_opt_inline_value(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = init_str_opt("output", "o", false);
    clp_add_command_option(root, output);

    char    *argv[] = {"prog", "-ofile.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(output->value.value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* POSIX: -o VALUE (value is next argv token) */
static void test_short_opt_next_argv_value(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = init_str_opt("output", "o", false);
    clp_add_command_option(root, output);

    char    *argv[] = {"prog", "-o", "file.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(output->value.value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* GNU: --output=VALUE */
static void test_long_opt_inline_eq_value(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = init_str_opt("output", "o", false);
    clp_add_command_option(root, output);

    char    *argv[] = {"prog", "--output=file.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(output->value.value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* GNU: --output VALUE */
static void test_long_opt_next_argv_value(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = init_str_opt("output", "o", false);
    clp_add_command_option(root, output);

    char    *argv[] = {"prog", "--output", "file.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(output->value.value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* POSIX: -abVALUE — combined bools followed by value-taking option inline */
static void test_combined_bools_then_inline_value_opt(void)
{
    Command *root;
    Option  *fa, *fb, *output;
    root   = clp_new_command(0, "prog", NULL);
    fa     = init_bool_opt("aaa", "a", false);
    fb     = init_bool_opt("bbb", "b", false);
    output = init_str_opt("output", "o", false);
    clp_add_command_option(root, fa);
    clp_add_command_option(root, fb);
    clp_add_command_option(root, output);

    char    *argv[] = {"prog", "-abofile.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == true);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(output->value.value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* ── count options ──────────────────────────────────────────────────────── */

static void test_count_option_increments_once(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_count_opt("verbose", "v");
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", "-v", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value.value_usize == 1);
    clp_cleanup(root);
}

static void test_count_option_increments_multiple_times(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_count_opt("verbose", "v");
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", "-v", "-v", "-v", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value.value_usize == 3);
    clp_cleanup(root);
}

/* POSIX: -vvv combined should count 3 */
static void test_count_option_combined_short(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_count_opt("verbose", "v");
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", "-vvv", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value.value_usize == 3);
    clp_cleanup(root);
}

static void test_count_option_mixed_short_and_long(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_count_opt("verbose", "v");
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", "-v", "--verbose", "-vv", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value.value_usize == 4);
    clp_cleanup(root);
}

/* ── list options ───────────────────────────────────────────────────────── */

static void test_list_option_single_entry(void)
{
    Command *root;
    Option  *files;
    root  = clp_new_command(0, "prog", NULL);
    files = init_list_opt("files", "f");
    clp_add_command_option(root, files);

    char    *argv[] = {"prog", "--files", "a.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(files->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&files->value.value_list) == 1);
    clp_cleanup(root);
}

static void test_list_option_comma_separated(void)
{
    Command *root;
    Option  *files;
    root  = clp_new_command(0, "prog", NULL);
    files = init_list_opt("files", "f");
    clp_add_command_option(root, files);

    char    *argv[] = {"prog", "--files", "a.txt,b.txt,c.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(files->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&files->value.value_list) == 3);

    DStringView tok;
    D_TEST_EXPR(d_dyn_array_get_elem_at(&files->value.value_list, 0, &tok) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(tok, "a.txt"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&files->value.value_list, 1, &tok) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(tok, "b.txt"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&files->value.value_list, 2, &tok) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(tok, "c.txt"));
    clp_cleanup(root);
}

/* ── operand parsing ────────────────────────────────────────────────────── */

static void test_single_opnd_is_set(void)
{
    Command *root;
    Operand  file;
    root = clp_new_command(0, "prog", NULL);
    init_str_operand(&file, "file", false);
    clp_add_command_operand(root, &file);

    char    *argv[] = {"prog", "input.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(file.value_set == true);
    D_TEST_STR_EQ(file.value.value_d_string_view.data, "input.txt");
    clp_cleanup(root);
}

static void test_options_and_opnd_together(void)
{
    Command *root;
    Option  *verbose;
    Operand  file;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    init_str_operand(&file, "file", false);
    clp_add_command_option(root, verbose);
    clp_add_command_operand(root, &file);

    char    *argv[] = {"prog", "-v", "input.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(file.value_set == true);
    clp_cleanup(root);
}

/* GNU: options can appear after operands (non-POSIX scanning) */
static void test_option_after_operand(void)
{
    Command *root;
    Option  *verbose;
    Operand  file;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    init_str_operand(&file, "file", false);
    clp_add_command_option(root, verbose);
    clp_add_command_operand(root, &file);

    char    *argv[] = {"prog", "input.txt", "-v", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(file.value_set == true);
    D_TEST_EXPR(verbose->value_set == true);
    clp_cleanup(root);
}

static void test_multiple_operands(void)
{
    Command *root;
    Operand  src, dst;
    root = clp_new_command(0, "prog", NULL);
    init_str_operand(&src, "src", false);
    init_str_operand(&dst, "dst", false);
    clp_add_command_operand(root, &src);
    clp_add_command_operand(root, &dst);

    char    *argv[] = {"prog", "a.txt", "b.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(src.value_set == true);
    D_TEST_EXPR(dst.value_set == true);
    D_TEST_STR_EQ(src.value.value_d_string_view.data, "a.txt");
    D_TEST_STR_EQ(dst.value.value_d_string_view.data, "b.txt");
    clp_cleanup(root);
}

/* POSIX/GNU: -- terminates option parsing; everything after is an operand */
static void test_double_hyphen_terminates_option_parsing(void)
{
    Command *root;
    Option  *verbose;
    Operand  file;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    init_str_operand(&file, "file", false);
    clp_add_command_option(root, verbose);
    clp_add_command_operand(root, &file);

    char    *argv[] = {"prog", "--", "--verbose", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == false);
    D_TEST_EXPR(file.value_set == true);
    D_TEST_STR_EQ(file.value.value_d_string_view.data, "--verbose");
    clp_cleanup(root);
}

static void test_double_hyphen_with_options_before(void)
{
    Command *root;
    Option  *verbose;
    Operand  file;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    init_str_operand(&file, "file", false);
    clp_add_command_option(root, verbose);
    clp_add_command_operand(root, &file);

    char    *argv[] = {"prog", "-v", "--", "file.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(file.value_set == true);
    D_TEST_STR_EQ(file.value.value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* ── list operands ──────────────────────────────────────────────────────── */

static void test_list_opnd_consumes_remaining_args(void)
{
    Command *root;
    Operand  files;
    root = clp_new_command(0, "prog", NULL);
    init_list_operand(&files, "files", false);
    clp_add_command_operand(root, &files);

    char    *argv[] = {"prog", "a.txt", "b.txt", "c.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(files.value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&files.value.value_list) == 3);
    clp_cleanup(root);
}

/* ── subcommand dispatch ────────────────────────────────────────────────── */

static void test_subcommand_is_dispatched(void)
{
    Command *root, *add;
    root = clp_new_command(0, "prog", NULL);
    add  = clp_new_command(1, "add", NULL);
    clp_add_command_sub_command(root, add);

    char    *argv[] = {"prog", "add", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    clp_cleanup(root);
}

static void test_subcommand_with_own_option(void)
{
    Command *root, *commit;
    Option  *msg;
    root   = clp_new_command(0, "prog", NULL);
    commit = clp_new_command(1, "commit", NULL);
    msg    = init_bool_opt("amend", "a", false);
    clp_add_command_option(commit, msg);
    clp_add_command_sub_command(root, commit);

    char    *argv[] = {"prog", "commit", "--amend", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == commit);
    D_TEST_EXPR(msg->value_set == true);
    D_TEST_EXPR(msg->value.value_bool == true);
    clp_cleanup(root);
}

static void test_no_subcommand_leaves_command_null(void)
{
    Command *root, *add;
    root = clp_new_command(0, "prog", NULL);
    add  = clp_new_command(1, "add", NULL);
    clp_add_command_sub_command(root, add);

    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_NULL(cmd);
    clp_cleanup(root);
}

static void test_multiple_subcommands_dispatch_correct_one(void)
{
    Command *root, *add, *rm;
    root = clp_new_command(0, "prog", NULL);
    add  = clp_new_command(1, "add", NULL);
    rm   = clp_new_command(2, "rm", NULL);
    clp_add_command_sub_command(root, add);
    clp_add_command_sub_command(root, rm);

    char    *argv[] = {"prog", "rm", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == rm);
    clp_cleanup(root);
}

/* ── type conversions ───────────────────────────────────────────────────── */

static void test_usize_option_parses_decimal(void)
{
    Command *root;
    Option  *jobs;
    root = clp_new_command(0, "prog", NULL);
    jobs = clp_new_option("jobs", "j", NULL, TYPE_USIZE, false, false);
    clp_add_command_option(root, jobs);

    char    *argv[] = {"prog", "--jobs", "4", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(jobs->value_set == true);
    D_TEST_EXPR(jobs->value.value_usize == 4);
    clp_cleanup(root);
}

static void test_long_option_parses_negative(void)
{
    Command *root;
    Option  *level;
    root  = clp_new_command(0, "prog", NULL);
    level = clp_new_option("level", "l", NULL, TYPE_LONG, false, false);
    clp_add_command_option(root, level);

    char    *argv[] = {"prog", "--level", "-5", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(level->value_set == true);
    D_TEST_EXPR(level->value.value_long == -5);
    clp_cleanup(root);
}

static void test_char_option_parses_single_char(void)
{
    Command *root;
    Option  *sep;
    root = clp_new_command(0, "prog", NULL);
    sep  = clp_new_option("sep", "s", NULL, TYPE_CHAR, false, false);
    clp_add_command_option(root, sep);

    char    *argv[] = {"prog", "--sep", ",", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(sep->value_set == true);
    D_TEST_EXPR(sep->value.value_char == ',');
    clp_cleanup(root);
}

/* ── edge cases ─────────────────────────────────────────────────────────── */

/* --output= (empty value after =) */
static void test_long_opt_empty_inline_value(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = init_str_opt("output", "o", false);
    clp_add_command_option(root, output);

    char    *argv[] = {"prog", "--output=", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(output->value.value_d_string_view.data, "");
    clp_cleanup(root);
}

/* options mixed before and after operands (GNU scanning) */
static void test_option_interleaved_with_operands(void)
{
    Command *root;
    Option  *verbose, *dry;
    Operand  src, dst;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    dry     = init_bool_opt("dry-run", "n", false);
    init_str_operand(&src, "src", false);
    init_str_operand(&dst, "dst", false);
    clp_add_command_option(root, verbose);
    clp_add_command_option(root, dry);
    clp_add_command_operand(root, &src);
    clp_add_command_operand(root, &dst);

    char    *argv[] = {"prog", "src.txt", "-v", "dst.txt", "-n", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(dry->value_set == true);
    D_TEST_EXPR(src.value_set == true);
    D_TEST_EXPR(dst.value_set == true);
    clp_cleanup(root);
}

/* bool option can be set multiple times (not OPT_ACT_SET_UNIQUE) */
static void test_bool_option_set_twice_is_ok(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    clp_add_command_option(root, verbose);

    char    *argv[] = {"prog", "-v", "--verbose", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(verbose->value.value_bool == true);
    clp_cleanup(root);
}

/* no-arg invocation with only optional options — parses cleanly */
static void test_no_args_with_optional_options(void)
{
    Command *root;
    Option  *a, *b;
    root = clp_new_command(0, "prog", NULL);
    a    = init_bool_opt("aaa", "a", false);
    b    = init_bool_opt("bbb", "b", false);
    clp_add_command_option(root, a);
    clp_add_command_option(root, b);

    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(a->value_set == false);
    D_TEST_EXPR(b->value_set == false);
    clp_cleanup(root);
}

/* long option with hyphen in name: --dry-run */
static void test_long_opt_with_hyphen_in_name(void)
{
    Command *root;
    Option  *dry;
    root = clp_new_command(0, "prog", NULL);
    dry  = init_bool_opt("dry-run", "n", false);
    clp_add_command_option(root, dry);

    char    *argv[] = {"prog", "--dry-run", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(dry->value_set == true);
    clp_cleanup(root);
}

/* ── nested subcommands ─────────────────────────────────────────────────── */

/* prog cmd subcmd — 2-level dispatch, returned command is innermost */
static void test_two_level_subcommand_dispatch(void)
{
    Command *root, *remote, *add;
    root   = clp_new_command(0, "prog", NULL);
    remote = clp_new_command(1, "remote", NULL);
    add    = clp_new_command(2, "add", NULL);
    clp_add_command_sub_command(root, remote);
    clp_add_command_sub_command(remote, add);

    char    *argv[] = {"prog", "remote", "add", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    clp_cleanup(root);
}

/* prog cmd subcmd subsubcmd — 3-level dispatch */
static void test_three_level_subcommand_dispatch(void)
{
    Command *root, *remote, *add, *origin;
    root   = clp_new_command(0, "prog", NULL);
    remote = clp_new_command(1, "remote", NULL);
    add    = clp_new_command(2, "add", NULL);
    origin = clp_new_command(3, "origin", NULL);
    clp_add_command_sub_command(root, remote);
    clp_add_command_sub_command(remote, add);
    clp_add_command_sub_command(add, origin);

    char    *argv[] = {"prog", "remote", "add", "origin", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == origin);
    clp_cleanup(root);
}

/* parent_command pointer is set correctly for each level */
static void test_parent_command_pointer_chain(void)
{
    Command *root, *lvl1, *lvl2;
    root = clp_new_command(0, "prog", NULL);
    lvl1 = clp_new_command(1, "cmd", NULL);
    lvl2 = clp_new_command(2, "sub", NULL);
    clp_add_command_sub_command(root, lvl1);
    clp_add_command_sub_command(lvl1, lvl2);

    D_TEST_EXPR(lvl1->parent_command == root);
    D_TEST_EXPR(lvl2->parent_command == lvl1);
    clp_cleanup(root);
}

/* root option before subcommand is parsed at root scope */
static void test_root_option_before_subcommand(void)
{
    Command *root, *push;
    Option  *verbose, *force;
    root    = clp_new_command(0, "prog", NULL);
    push    = clp_new_command(1, "push", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    force   = init_bool_opt("force", "f", false);
    clp_add_command_option(root, verbose);
    clp_add_command_option(push, force);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "--verbose", "push", "--force", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* root option is unset when subcommand option is the only thing provided */
static void test_root_option_unset_when_only_sub_option_given(void)
{
    Command *root, *push;
    Option  *verbose, *force;
    root    = clp_new_command(0, "prog", NULL);
    push    = clp_new_command(1, "push", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    force   = init_bool_opt("force", "f", false);
    clp_add_command_option(root, verbose);
    clp_add_command_option(push, force);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "push", "--force", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == false);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* options at all 3 levels each get correctly assigned */
static void test_options_at_each_of_three_levels(void)
{
    Command *root, *remote, *add;
    Option  *verbose, *url, *force;
    Operand  name;
    root    = clp_new_command(0, "prog", NULL);
    remote  = clp_new_command(1, "remote", NULL);
    add     = clp_new_command(2, "add", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    url     = init_str_opt("url", "u", false);
    force   = init_bool_opt("force", "f", false);
    init_str_operand(&name, "name", false);
    clp_add_command_option(root, verbose);
    clp_add_command_option(remote, url);
    clp_add_command_option(add, force);
    clp_add_command_operand(add, &name);
    clp_add_command_sub_command(root, remote);
    clp_add_command_sub_command(remote, add);

    char    *argv[] = {"prog", "--verbose", "remote", "--url", "git@x.com", "add", "--force", "origin", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(url->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    D_TEST_EXPR(name.value_set == true);
    D_TEST_STR_EQ(url->value.value_d_string_view.data, "git@x.com");
    D_TEST_STR_EQ(name.value.value_d_string_view.data, "origin");
    clp_cleanup(root);
}

/* sibling subcommands are isolated — parsing one doesn't affect the other */
static void test_sibling_subcommand_options_are_isolated(void)
{
    Command *root, *add, *rm;
    Option  *force, *recursive;
    root      = clp_new_command(0, "prog", NULL);
    add       = clp_new_command(1, "add", NULL);
    rm        = clp_new_command(2, "rm", NULL);
    force     = init_bool_opt("force", "f", false);
    recursive = init_bool_opt("recursive", "r", false);
    clp_add_command_option(add, force);
    clp_add_command_option(rm, recursive);
    clp_add_command_sub_command(root, add);
    clp_add_command_sub_command(root, rm);

    char    *argv[] = {"prog", "rm", "--recursive", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == rm);
    D_TEST_EXPR(recursive->value_set == true);
    D_TEST_EXPR(force->value_set == false);
    clp_cleanup(root);
}

/* subcommand with both options and operands */
static void test_subcommand_with_options_and_operands(void)
{
    Command *root, *commit;
    Option  *amend;
    Operand  msg;
    root   = clp_new_command(0, "prog", NULL);
    commit = clp_new_command(1, "commit", NULL);
    amend  = init_bool_opt("amend", "a", false);
    init_str_operand(&msg, "message", false);
    clp_add_command_option(commit, amend);
    clp_add_command_operand(commit, &msg);
    clp_add_command_sub_command(root, commit);

    char    *argv[] = {"prog", "commit", "--amend", "my message", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == commit);
    D_TEST_EXPR(amend->value_set == true);
    D_TEST_EXPR(msg.value_set == true);
    D_TEST_STR_EQ(msg.value.value_d_string_view.data, "my message");
    clp_cleanup(root);
}

/* adding a subcommand to a command that already has operands is rejected */
static void _err_add_sub_when_operands_exist(void)
{
    Command *root, *push;
    Operand  file;
    root = clp_new_command(0, "prog", NULL);
    push = clp_new_command(1, "push", NULL);
    init_str_operand(&file, "file", false);
    clp_add_command_operand(root, &file);
    clp_add_command_sub_command(root, push);
}
static void test_add_subcommand_when_operands_exist_exits(void)
{
    ChildResult r = run_child(_err_add_sub_when_operands_exist);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot have both operands and subcommands"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
}

/* adding an operand to a command that already has subcommands is rejected */
static void _err_add_opnd_when_subcommands_exist(void)
{
    Command *root, *push;
    Operand  file;
    root = clp_new_command(0, "prog", NULL);
    push = clp_new_command(1, "push", NULL);
    init_str_operand(&file, "file", false);
    clp_add_command_sub_command(root, push);
    clp_add_command_operand(root, &file);
}
static void test_add_opnd_when_subcommands_exist_exits(void)
{
    ChildResult r = run_child(_err_add_opnd_when_subcommands_exist);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot have both operands and subcommands"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
}

/* -- in subcommand context terminates its option parsing */
static void test_double_hyphen_in_subcommand_context(void)
{
    Command *root, *add;
    Option  *force;
    Operand  file;
    root  = clp_new_command(0, "prog", NULL);
    add   = clp_new_command(1, "add", NULL);
    force = init_bool_opt("force", "f", false);
    init_str_operand(&file, "file", false);
    clp_add_command_option(add, force);
    clp_add_command_operand(add, &file);
    clp_add_command_sub_command(root, add);

    char    *argv[] = {"prog", "add", "--", "--force", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(force->value_set == false);
    D_TEST_EXPR(file.value_set == true);
    D_TEST_STR_EQ(file.value.value_d_string_view.data, "--force");
    clp_cleanup(root);
}

/* combined short flags in subcommand context */
static void test_combined_short_flags_in_subcommand(void)
{
    Command *root, *add;
    Option  *recurse, *force;
    root    = clp_new_command(0, "prog", NULL);
    add     = clp_new_command(1, "add", NULL);
    recurse = init_bool_opt("recursive", "r", false);
    force   = init_bool_opt("force", "f", false);
    clp_add_command_option(add, recurse);
    clp_add_command_option(add, force);
    clp_add_command_sub_command(root, add);

    char    *argv[] = {"prog", "add", "-rf", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(recurse->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* count option in subcommand context */
static void test_count_option_in_subcommand(void)
{
    Command *root, *run;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    run     = clp_new_command(1, "run", NULL);
    verbose = init_count_opt("verbose", "v");
    clp_add_command_option(run, verbose);
    clp_add_command_sub_command(root, run);

    char    *argv[] = {"prog", "run", "-vvv", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == run);
    D_TEST_EXPR(verbose->value.value_usize == 3);
    clp_cleanup(root);
}

/* list option in subcommand context */
static void test_list_option_in_subcommand(void)
{
    Command *root, *build;
    Option  *features;
    root     = clp_new_command(0, "prog", NULL);
    build    = clp_new_command(1, "build", NULL);
    features = init_list_opt("features", "F");
    clp_add_command_option(build, features);
    clp_add_command_sub_command(root, build);

    char    *argv[] = {"prog", "build", "--features", "x,y,z", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == build);
    D_TEST_EXPR(features->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&features->value.value_list) == 3);
    clp_cleanup(root);
}

/* long opt with = value in subcommand context */
static void test_long_opt_eq_value_in_subcommand(void)
{
    Command *root, *clone;
    Option  *depth;
    root  = clp_new_command(0, "prog", NULL);
    clone = clp_new_command(1, "clone", NULL);
    depth = clp_new_option("depth", "d", NULL, TYPE_USIZE, false, false);
    clp_add_command_option(clone, depth);
    clp_add_command_sub_command(root, clone);

    char    *argv[] = {"prog", "clone", "--depth=1", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == clone);
    D_TEST_EXPR(depth->value_set == true);
    D_TEST_EXPR(depth->value.value_usize == 1);
    clp_cleanup(root);
}

/* subcommand with multiple operands, all set */
static void test_subcommand_with_multiple_operands(void)
{
    Command *root, *cp;
    Operand  src, dst;
    root = clp_new_command(0, "prog", NULL);
    cp   = clp_new_command(1, "cp", NULL);
    init_str_operand(&src, "src", false);
    init_str_operand(&dst, "dst", false);
    clp_add_command_operand(cp, &src);
    clp_add_command_operand(cp, &dst);
    clp_add_command_sub_command(root, cp);

    char    *argv[] = {"prog", "cp", "a.txt", "b.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == cp);
    D_TEST_EXPR(src.value_set == true);
    D_TEST_EXPR(dst.value_set == true);
    D_TEST_STR_EQ(src.value.value_d_string_view.data, "a.txt");
    D_TEST_STR_EQ(dst.value.value_d_string_view.data, "b.txt");
    clp_cleanup(root);
}

/* three sibling subcommands, third one dispatched */
static void test_three_siblings_dispatch_third(void)
{
    Command *root, *add, *rm, *ls;
    root = clp_new_command(0, "prog", NULL);
    add  = clp_new_command(1, "add", NULL);
    rm   = clp_new_command(2, "rm", NULL);
    ls   = clp_new_command(3, "ls", NULL);
    clp_add_command_sub_command(root, add);
    clp_add_command_sub_command(root, rm);
    clp_add_command_sub_command(root, ls);

    char    *argv[] = {"prog", "ls", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == ls);
    clp_cleanup(root);
}

/* subcommand code is correct for identification */
static void test_subcommand_code_is_correct(void)
{
    Command *root, *add, *rm;
    root = clp_new_command(0, "prog", NULL);
    add  = clp_new_command(42, "add", NULL);
    rm   = clp_new_command(99, "rm", NULL);
    clp_add_command_sub_command(root, add);
    clp_add_command_sub_command(root, rm);

    char    *argv[] = {"prog", "rm", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd->code == 99);
    clp_cleanup(root);
}

/* subcommand with inline short value and its own operand */
static void test_subcommand_short_inline_value_and_operand(void)
{
    Command *root, *push;
    Option  *remote;
    Operand  branch;
    root   = clp_new_command(0, "prog", NULL);
    push   = clp_new_command(1, "push", NULL);
    remote = init_str_opt("remote", "r", false);
    init_str_operand(&branch, "branch", false);
    clp_add_command_option(push, remote);
    clp_add_command_operand(push, &branch);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "push", "-rorigin", "main", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(remote->value_set == true);
    D_TEST_EXPR(branch.value_set == true);
    D_TEST_STR_EQ(remote->value.value_d_string_view.data, "origin");
    D_TEST_STR_EQ(branch.value.value_d_string_view.data, "main");
    clp_cleanup(root);
}

/* deep 3-level: options at level 1 and 3, not level 2 */
static void test_three_level_options_at_outer_and_inner(void)
{
    Command *root, *grp, *cmd;
    Option  *quiet, *debug;
    root  = clp_new_command(0, "prog", NULL);
    grp   = clp_new_command(1, "grp", NULL);
    cmd   = clp_new_command(2, "cmd", NULL);
    quiet = init_bool_opt("quiet", "q", false);
    debug = init_bool_opt("debug", "d", false);
    clp_add_command_option(root, quiet);
    clp_add_command_option(cmd, debug);
    clp_add_command_sub_command(root, grp);
    clp_add_command_sub_command(grp, cmd);

    char    *argv[]     = {"prog", "-q", "grp", "cmd", "--debug", NULL};
    Command *dispatched = NULL;
    clp_parse_args(root, argv, &dispatched);
    D_TEST_EXPR(dispatched == cmd);
    D_TEST_EXPR(quiet->value_set == true);
    D_TEST_EXPR(debug->value_set == true);
    clp_cleanup(root);
}

/* subcommand options don't bleed into sibling that was registered later */
static void test_sibling_registered_after_does_not_bleed(void)
{
    Command *root, *fetch, *pull;
    Option  *all_fetch, *rebase;
    root      = clp_new_command(0, "prog", NULL);
    fetch     = clp_new_command(1, "fetch", NULL);
    pull      = clp_new_command(2, "pull", NULL);
    all_fetch = init_bool_opt("all", "a", false);
    rebase    = init_bool_opt("rebase", "r", false);
    clp_add_command_option(fetch, all_fetch);
    clp_add_command_option(pull, rebase);
    clp_add_command_sub_command(root, fetch);
    clp_add_command_sub_command(root, pull);

    char    *argv[] = {"prog", "fetch", "--all", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == fetch);
    D_TEST_EXPR(all_fetch->value_set == true);
    D_TEST_EXPR(rebase->value_set == false);
    clp_cleanup(root);
}

/* option before 2-level nested subcommand */
static void test_root_option_before_two_level_subcommand(void)
{
    Command *root, *remote, *add;
    Option  *verbose, *force;
    root    = clp_new_command(0, "prog", NULL);
    remote  = clp_new_command(1, "remote", NULL);
    add     = clp_new_command(2, "add", NULL);
    verbose = init_bool_opt("verbose", "v", false);
    force   = init_bool_opt("force", "f", false);
    clp_add_command_option(root, verbose);
    clp_add_command_option(add, force);
    clp_add_command_sub_command(root, remote);
    clp_add_command_sub_command(remote, add);

    char    *argv[] = {"prog", "-v", "remote", "add", "-f", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* no args to program with subcommands registered — command stays NULL */
static void test_no_args_with_subcommands_registered(void)
{
    Command *root, *add, *rm;
    root = clp_new_command(0, "prog", NULL);
    add  = clp_new_command(1, "add", NULL);
    rm   = clp_new_command(2, "rm", NULL);
    clp_add_command_sub_command(root, add);
    clp_add_command_sub_command(root, rm);

    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_NULL(cmd);
    clp_cleanup(root);
}

/* subcommand with list operand consumes all trailing args */
static void test_subcommand_with_list_operand(void)
{
    Command *root, *add;
    Operand  files;
    root = clp_new_command(0, "prog", NULL);
    add  = clp_new_command(1, "add", NULL);
    init_list_operand(&files, "files", false);
    clp_add_command_operand(add, &files);
    clp_add_command_sub_command(root, add);

    char    *argv[] = {"prog", "add", "a.c", "b.c", "c.c", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(files.value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&files.value.value_list) == 3);
    clp_cleanup(root);
}

/* ── global options inherited from parent to child ──────────────────────── */

/* global bool option set before subcommand is accessible after dispatch */
static void test_global_option_set_at_parent_accessible_after_dispatch(void)
{
    Command *root, *push;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    push    = clp_new_command(1, "push", NULL);
    verbose = clp_new_option("verbose", "v", NULL, TYPE_BOOL, false, true);
    clp_add_command_option(root, verbose);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "--verbose", "push", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(verbose->value_set == true);
    clp_cleanup(root);
}

/* required global option set at parent level satisfies child required check */
static void test_required_global_option_set_at_parent_satisfies_child(void)
{
    Command *root, *push;
    Option  *token;
    root  = clp_new_command(0, "prog", NULL);
    push  = clp_new_command(1, "push", NULL);
    token = clp_new_option("token", "t", NULL, TYPE_STR, true, true);
    clp_add_command_option(root, token);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "--token", "abc", "push", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(token->value_set == true);
    D_TEST_STR_EQ(token->value.value_d_string_view.data, "abc");
    clp_cleanup(root);
}

/* required global option NOT set — child required check catches it when child has args */
static void _err_required_global_option_unset_in_child(void)
{
    Command *root, *push;
    Option  *token, *force;
    root  = clp_new_command(0, "prog", NULL);
    push  = clp_new_command(1, "push", NULL);
    token = clp_new_option("token", "t", NULL, TYPE_STR, true, true);
    force = init_bool_opt("force", "f", false);
    clp_add_command_option(root, token);
    clp_add_command_option(push, force);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "push", "--force", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_required_global_option_unset_exits_via_child(void)
{
    ChildResult r = run_child(_err_required_global_option_unset_in_child);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--token"));
    D_TEST_NOT_NULL(strstr(r.err, "push"));
}

/* non-global required option on parent is NOT inherited — child check ignores it */
static void test_non_global_required_option_not_inherited_to_child(void)
{
    Command *root, *push;
    Option  *token;
    root  = clp_new_command(0, "prog", NULL);
    push  = clp_new_command(1, "push", NULL);
    token = clp_new_option("token", "t", NULL, TYPE_STR, true, false);
    clp_add_command_option(root, token);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "push", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(token->value_set == false);
    clp_cleanup(root);
}

/* global option propagates all the way to a deeply nested command */
static void test_global_option_inherited_through_nested_levels(void)
{
    Command *root, *remote, *add;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    remote  = clp_new_command(1, "remote", NULL);
    add     = clp_new_command(2, "add", NULL);
    verbose = clp_new_option("verbose", "v", NULL, TYPE_BOOL, false, true);
    clp_add_command_option(root, verbose);
    clp_add_command_sub_command(root, remote);
    clp_add_command_sub_command(remote, add);

    char    *argv[] = {"prog", "--verbose", "remote", "add", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(verbose->value_set == true);
    clp_cleanup(root);
}

/* multiple global options from parent are all inherited */
static void test_multiple_global_options_all_inherited(void)
{
    Command *root, *push;
    Option  *verbose, *dry_run;
    root    = clp_new_command(0, "prog", NULL);
    push    = clp_new_command(1, "push", NULL);
    verbose = clp_new_option("verbose", "v", NULL, TYPE_BOOL, false, true);
    dry_run = clp_new_option("dry-run", "n", NULL, TYPE_BOOL, false, true);
    clp_add_command_option(root, verbose);
    clp_add_command_option(root, dry_run);
    clp_add_command_sub_command(root, push);

    char    *argv[] = {"prog", "--verbose", "--dry-run", "push", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(dry_run->value_set == true);
    clp_cleanup(root);
}

/* ── 5-level deep command with 20 options and 20 operands ───────────────── */

static void test_five_level_deep_with_many_options_and_operands(void)
{
    /* hierarchy: prog → cluster → node → process → task → run (5 levels) */
    Command *root, *cluster, *node, *process, *task, *run;
    root    = clp_new_command(0, "prog", NULL);
    cluster = clp_new_command(1, "cluster", NULL);
    node    = clp_new_command(2, "node", NULL);
    process = clp_new_command(3, "process", NULL);
    task    = clp_new_command(4, "task", NULL);
    run     = clp_new_command(5, "run", NULL);

    /* one option at each intermediate level */
    Option *trace, *region, *node_id, *proc_pid, *task_pri;
    trace    = init_bool_opt("trace", "t", false);
    region   = init_str_opt("region", "R", false);
    node_id  = clp_new_option("node-id", "N", NULL, TYPE_USIZE, false, false);
    proc_pid = clp_new_option("pid", "P", NULL, TYPE_LONG, false, false);
    task_pri = clp_new_option("priority", "p", NULL, TYPE_CHAR, false, false);

    clp_add_command_option(root, trace);
    clp_add_command_option(cluster, region);
    clp_add_command_option(node, node_id);
    clp_add_command_option(process, proc_pid);
    clp_add_command_option(task, task_pri);

    /* 20 options on "run": 10 bools + 5 str + 2 usize + 1 long + 1 char + 1 count */
    Option *verbose, *dry_run, *force, *quiet, *all, *recursive, *no_cache, *strict, *opt_async, *parallel;
    Option *output, *config, *format, *target, *profile;
    Option *jobs, *retries, *timeout, *sep, *debug;

    verbose   = init_bool_opt("verbose", "v", false);
    dry_run   = init_bool_opt("dry-run", "n", false);
    force     = init_bool_opt("force", "f", false);
    quiet     = init_bool_opt("quiet", "q", false);
    all       = init_bool_opt("all", "a", false);
    recursive = init_bool_opt("recursive", "r", false);
    no_cache  = init_bool_opt("no-cache", "C", false);
    strict    = init_bool_opt("strict", "s", false);
    opt_async = init_bool_opt("async", "A", false);
    parallel  = init_bool_opt("parallel", "L", false);

    output  = init_str_opt("output", "o", false);
    config  = init_str_opt("config", "c", false);
    format  = init_str_opt("format", "F", false);
    target  = init_str_opt("target", "T", false);
    profile = init_str_opt("profile", "e", false);

    jobs    = clp_new_option("jobs", "j", NULL, TYPE_USIZE, false, false);
    retries = clp_new_option("retries", "x", NULL, TYPE_USIZE, false, false);
    timeout = clp_new_option("timeout", "m", NULL, TYPE_LONG, false, false);
    sep     = clp_new_option("separator", "S", NULL, TYPE_CHAR, false, false);
    debug   = init_count_opt("debug", "d");

    clp_add_command_option(run, verbose);
    clp_add_command_option(run, dry_run);
    clp_add_command_option(run, force);
    clp_add_command_option(run, quiet);
    clp_add_command_option(run, all);
    clp_add_command_option(run, recursive);
    clp_add_command_option(run, no_cache);
    clp_add_command_option(run, strict);
    clp_add_command_option(run, opt_async);
    clp_add_command_option(run, parallel);
    clp_add_command_option(run, output);
    clp_add_command_option(run, config);
    clp_add_command_option(run, format);
    clp_add_command_option(run, target);
    clp_add_command_option(run, profile);
    clp_add_command_option(run, jobs);
    clp_add_command_option(run, retries);
    clp_add_command_option(run, timeout);
    clp_add_command_option(run, sep);
    clp_add_command_option(run, debug);

    /* 20 operands on "run": 19 SET (op01-op19) + 1 LIST (files) */
    Operand op01, op02, op03, op04, op05, op06, op07, op08, op09, op10;
    Operand op11, op12, op13, op14, op15, op16, op17, op18, op19, files;

    init_str_operand(&op01, "op01", false);
    init_str_operand(&op02, "op02", false);
    init_str_operand(&op03, "op03", false);
    init_str_operand(&op04, "op04", false);
    init_str_operand(&op05, "op05", false);
    init_str_operand(&op06, "op06", false);
    init_str_operand(&op07, "op07", false);
    init_str_operand(&op08, "op08", false);
    init_str_operand(&op09, "op09", false);
    init_str_operand(&op10, "op10", false);
    init_str_operand(&op11, "op11", false);
    init_str_operand(&op12, "op12", false);
    init_str_operand(&op13, "op13", false);
    init_str_operand(&op14, "op14", false);
    init_str_operand(&op15, "op15", false);
    init_str_operand(&op16, "op16", false);
    init_str_operand(&op17, "op17", false);
    init_str_operand(&op18, "op18", false);
    init_str_operand(&op19, "op19", false);
    init_list_operand(&files, "files", false);

    clp_add_command_operand(run, &op01);
    clp_add_command_operand(run, &op02);
    clp_add_command_operand(run, &op03);
    clp_add_command_operand(run, &op04);
    clp_add_command_operand(run, &op05);
    clp_add_command_operand(run, &op06);
    clp_add_command_operand(run, &op07);
    clp_add_command_operand(run, &op08);
    clp_add_command_operand(run, &op09);
    clp_add_command_operand(run, &op10);
    clp_add_command_operand(run, &op11);
    clp_add_command_operand(run, &op12);
    clp_add_command_operand(run, &op13);
    clp_add_command_operand(run, &op14);
    clp_add_command_operand(run, &op15);
    clp_add_command_operand(run, &op16);
    clp_add_command_operand(run, &op17);
    clp_add_command_operand(run, &op18);
    clp_add_command_operand(run, &op19);
    clp_add_command_operand(run, &files);

    /* wire hierarchy */
    clp_add_command_sub_command(root, cluster);
    clp_add_command_sub_command(cluster, node);
    clp_add_command_sub_command(node, process);
    clp_add_command_sub_command(process, task);
    clp_add_command_sub_command(task, run);

    char *argv[] = {"prog", "--trace",         /* root: bool long */
                    "cluster", "-Rus-east",    /* cluster: str short inline */
                    "node", "--node-id", "42", /* node: usize long next-arg */
                    "process", "--pid", "-7",  /* process: long (negative) next-arg */
                    "task", "--priority", "H", /* task: char long next-arg */
                    "run",
                    /* 10 bools — mix of long, short, combined */
                    "-v", "--dry-run", "-fqa", /* combined: force, quiet, all */
                    "-rCsAL",                  /* combined: recursive, no-cache, strict, async, parallel */
                    /* 5 strs — mix of =inline, next-arg, short inline */
                    "--output=result.txt", "--config", "config.yml", "-Fjson", "--target", "prod", "-emyprofile",
                    /* 2 usize */
                    "--jobs", "8", "--retries", "3",
                    /* 1 long (negative) */
                    "--timeout", "-60",
                    /* 1 char */
                    "--separator", ",",
                    /* 1 count: -ddd = 3 */
                    "-ddd",
                    /* 19 SET operands */
                    "val01", "val02", "val03", "val04", "val05", "val06", "val07", "val08", "val09", "val10", "val11", "val12", "val13", "val14", "val15", "val16", "val17", "val18", "val19",
                    /* LIST operand: 3 filenames */
                    "fa.c", "fb.c", "fc.c", NULL};

    Command *cmd = NULL;
    clp_parse_args(root, argv, &cmd);

    /* dispatch and code */
    D_TEST_EXPR(cmd == run);
    D_TEST_EXPR(cmd->code == 5);

    /* parent chain */
    D_TEST_EXPR(run->parent_command == task);
    D_TEST_EXPR(task->parent_command == process);
    D_TEST_EXPR(process->parent_command == node);
    D_TEST_EXPR(node->parent_command == cluster);
    D_TEST_EXPR(cluster->parent_command == root);
    D_TEST_NULL(root->parent_command);

    /* intermediate level options */
    D_TEST_EXPR(trace->value_set == true && trace->value.value_bool == true);
    D_TEST_EXPR(region->value_set == true);
    D_TEST_STR_EQ(region->value.value_d_string_view.data, "us-east");
    D_TEST_EXPR(node_id->value_set == true && node_id->value.value_usize == 42);
    D_TEST_EXPR(proc_pid->value_set == true && proc_pid->value.value_long == -7);
    D_TEST_EXPR(task_pri->value_set == true && task_pri->value.value_char == 'H');

    /* 10 bools */
    D_TEST_EXPR(verbose->value_set == true && verbose->value.value_bool == true);
    D_TEST_EXPR(dry_run->value_set == true && dry_run->value.value_bool == true);
    D_TEST_EXPR(force->value_set == true && force->value.value_bool == true);
    D_TEST_EXPR(quiet->value_set == true && quiet->value.value_bool == true);
    D_TEST_EXPR(all->value_set == true && all->value.value_bool == true);
    D_TEST_EXPR(recursive->value_set == true && recursive->value.value_bool == true);
    D_TEST_EXPR(no_cache->value_set == true && no_cache->value.value_bool == true);
    D_TEST_EXPR(strict->value_set == true && strict->value.value_bool == true);
    D_TEST_EXPR(opt_async->value_set == true && opt_async->value.value_bool == true);
    D_TEST_EXPR(parallel->value_set == true && parallel->value.value_bool == true);

    /* 5 strs */
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(output->value.value_d_string_view.data, "result.txt");
    D_TEST_EXPR(config->value_set == true);
    D_TEST_STR_EQ(config->value.value_d_string_view.data, "config.yml");
    D_TEST_EXPR(format->value_set == true);
    D_TEST_STR_EQ(format->value.value_d_string_view.data, "json");
    D_TEST_EXPR(target->value_set == true);
    D_TEST_STR_EQ(target->value.value_d_string_view.data, "prod");
    D_TEST_EXPR(profile->value_set == true);
    D_TEST_STR_EQ(profile->value.value_d_string_view.data, "myprofile");

    /* 2 usize + 1 long + 1 char + 1 count */
    D_TEST_EXPR(jobs->value_set == true && jobs->value.value_usize == 8);
    D_TEST_EXPR(retries->value_set == true && retries->value.value_usize == 3);
    D_TEST_EXPR(timeout->value_set == true && timeout->value.value_long == -60);
    D_TEST_EXPR(sep->value_set == true && sep->value.value_char == ',');
    D_TEST_EXPR(debug->value.value_usize == 3);

    /* 19 SET operands */
    D_TEST_EXPR(op01.value_set == true);
    D_TEST_STR_EQ(op01.value.value_d_string_view.data, "val01");
    D_TEST_EXPR(op02.value_set == true);
    D_TEST_STR_EQ(op02.value.value_d_string_view.data, "val02");
    D_TEST_EXPR(op03.value_set == true);
    D_TEST_STR_EQ(op03.value.value_d_string_view.data, "val03");
    D_TEST_EXPR(op04.value_set == true);
    D_TEST_STR_EQ(op04.value.value_d_string_view.data, "val04");
    D_TEST_EXPR(op05.value_set == true);
    D_TEST_STR_EQ(op05.value.value_d_string_view.data, "val05");
    D_TEST_EXPR(op06.value_set == true);
    D_TEST_STR_EQ(op06.value.value_d_string_view.data, "val06");
    D_TEST_EXPR(op07.value_set == true);
    D_TEST_STR_EQ(op07.value.value_d_string_view.data, "val07");
    D_TEST_EXPR(op08.value_set == true);
    D_TEST_STR_EQ(op08.value.value_d_string_view.data, "val08");
    D_TEST_EXPR(op09.value_set == true);
    D_TEST_STR_EQ(op09.value.value_d_string_view.data, "val09");
    D_TEST_EXPR(op10.value_set == true);
    D_TEST_STR_EQ(op10.value.value_d_string_view.data, "val10");
    D_TEST_EXPR(op11.value_set == true);
    D_TEST_STR_EQ(op11.value.value_d_string_view.data, "val11");
    D_TEST_EXPR(op12.value_set == true);
    D_TEST_STR_EQ(op12.value.value_d_string_view.data, "val12");
    D_TEST_EXPR(op13.value_set == true);
    D_TEST_STR_EQ(op13.value.value_d_string_view.data, "val13");
    D_TEST_EXPR(op14.value_set == true);
    D_TEST_STR_EQ(op14.value.value_d_string_view.data, "val14");
    D_TEST_EXPR(op15.value_set == true);
    D_TEST_STR_EQ(op15.value.value_d_string_view.data, "val15");
    D_TEST_EXPR(op16.value_set == true);
    D_TEST_STR_EQ(op16.value.value_d_string_view.data, "val16");
    D_TEST_EXPR(op17.value_set == true);
    D_TEST_STR_EQ(op17.value.value_d_string_view.data, "val17");
    D_TEST_EXPR(op18.value_set == true);
    D_TEST_STR_EQ(op18.value.value_d_string_view.data, "val18");
    D_TEST_EXPR(op19.value_set == true);
    D_TEST_STR_EQ(op19.value.value_d_string_view.data, "val19");

    /* LIST operand: 3 entries */
    D_TEST_EXPR(files.value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&files.value.value_list) == 3);
    DStringView fv;
    D_TEST_EXPR(d_dyn_array_get_elem_at(&files.value.value_list, 0, &fv) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(fv, "fa.c"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&files.value.value_list, 1, &fv) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(fv, "fb.c"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&files.value.value_list, 2, &fv) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(fv, "fc.c"));

    clp_cleanup(root);
}

/* ── error / exit tests ─────────────────────────────────────────────────── */

/* unknown long option */
static void _err_unknown_long_opt(void)
{
    Command *root;
    root            = clp_new_command(0, "prog", NULL);
    char    *argv[] = {"prog", "--unknown", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_unknown_long_option_exits(void)
{
    ChildResult r = run_child(_err_unknown_long_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "--unknown"));
}

/* unknown short option */
static void _err_unknown_short_opt(void)
{
    Command *root;
    root            = clp_new_command(0, "prog", NULL);
    char    *argv[] = {"prog", "-z", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_unknown_short_option_exits(void)
{
    ChildResult r = run_child(_err_unknown_short_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "-z"));
}

/* missing required option */
static void _err_missing_required_opt(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = clp_new_option("output", "o", NULL, TYPE_STR, true, false);
    clp_add_command_option(root, output);
    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_option_exits(void)
{
    ChildResult r = run_child(_err_missing_required_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--output"));
}

/* missing required operand */
static void _err_missing_required_operand(void)
{
    Command *root;
    Operand  file;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&file, "file", NULL, TYPE_STR, true);
    clp_add_command_operand(root, &file);
    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_opnd_exits(void)
{
    ChildResult r = run_child(_err_missing_required_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following operands were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "<file>"));
}

/* too many operands */
static void _err_too_many_operands(void)
{
    Command *root;
    Operand  file;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&file, "file", NULL, TYPE_STR, false);
    clp_add_command_operand(root, &file);
    char    *argv[] = {"prog", "a.txt", "b.txt", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_too_many_operands_exits(void)
{
    ChildResult r = run_child(_err_too_many_operands);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "too many operands"));
}

/* invalid value for TYPE_USIZE */
static void _err_invalid_usize_value(void)
{
    Command *root;
    Option  *jobs;
    root = clp_new_command(0, "prog", NULL);
    jobs = clp_new_option("jobs", "j", NULL, TYPE_USIZE, false, false);
    clp_add_command_option(root, jobs);
    char    *argv[] = {"prog", "--jobs", "notanumber", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_usize_value_exits(void)
{
    ChildResult r = run_child(_err_invalid_usize_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notanumber"));
    D_TEST_NOT_NULL(strstr(r.err, "--jobs"));
}

/* invalid value for TYPE_LONG */
static void _err_invalid_long_value(void)
{
    Command *root;
    Option  *level;
    root  = clp_new_command(0, "prog", NULL);
    level = clp_new_option("level", "l", NULL, TYPE_LONG, false, false);
    clp_add_command_option(root, level);
    char    *argv[] = {"prog", "--level", "abc", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_long_value_exits(void)
{
    ChildResult r = run_child(_err_invalid_long_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "abc"));
    D_TEST_NOT_NULL(strstr(r.err, "--level"));
}

/* invalid value for TYPE_CHAR: more than one character */
static void _err_invalid_char_too_many(void)
{
    Command *root;
    Option  *sep;
    root = clp_new_command(0, "prog", NULL);
    sep  = clp_new_option("sep", "s", NULL, TYPE_CHAR, false, false);
    clp_add_command_option(root, sep);
    char    *argv[] = {"prog", "--sep", "ab", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_char_value_too_many_chars_exits(void)
{
    ChildResult r = run_child(_err_invalid_char_too_many);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "too many characters"));
    D_TEST_NOT_NULL(strstr(r.err, "--sep"));
}

/* invalid value for TYPE_CHAR: empty string */
static void _err_invalid_char_empty(void)
{
    Command *root;
    Option  *sep;
    root = clp_new_command(0, "prog", NULL);
    sep  = clp_new_option("sep", "s", NULL, TYPE_CHAR, false, false);
    clp_add_command_option(root, sep);
    char    *argv[] = {"prog", "--sep", "", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_char_value_empty_string_exits(void)
{
    ChildResult r = run_child(_err_invalid_char_empty);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot parse char"));
    D_TEST_NOT_NULL(strstr(r.err, "--sep"));
}

/* bool option given inline value: --verbose=foo */
static void _err_bool_opt_inline_value(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = clp_new_option("verbose", "v", NULL, TYPE_BOOL, false, false);
    clp_add_command_option(root, verbose);
    char    *argv[] = {"prog", "--verbose=foo", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_bool_opt_with_inline_value_exits(void)
{
    ChildResult r = run_child(_err_bool_opt_inline_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "doesn't allow an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "--verbose"));
}

/* count option given inline value: --debug=3 */
static void _err_count_opt_inline_value(void)
{
    Command *root;
    Option  *debug;
    root  = clp_new_command(0, "prog", NULL);
    debug = clp_new_option_count("debug", "d", NULL, false);
    clp_add_command_option(root, debug);
    char    *argv[] = {"prog", "--debug=3", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_count_opt_with_inline_value_exits(void)
{
    ChildResult r = run_child(_err_count_opt_inline_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "doesn't allow an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "--debug"));
}

/* str option with no next argument (last in argv) */
static void _err_str_opt_no_value(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = clp_new_option("output", "o", NULL, TYPE_STR, false, false);
    clp_add_command_option(root, output);
    char    *argv[] = {"prog", "--output", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_str_opt_missing_value_exits(void)
{
    ChildResult r = run_child(_err_str_opt_no_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "option require an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "--output"));
}

/* duplicate long option name on same command */
static void _err_duplicate_long_name(void)
{
    Command *root;
    Option  *a, *b;
    root = clp_new_command(0, "prog", NULL);
    a    = clp_new_option("verbose", "v", NULL, TYPE_BOOL, false, false);
    b    = clp_new_option("verbose", "w", NULL, TYPE_BOOL, false, false);
    clp_add_command_option(root, a);
    clp_add_command_option(root, b);
}
static void test_duplicate_long_option_name_exits(void)
{
    ChildResult r = run_child(_err_duplicate_long_name);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "already registered"));
    D_TEST_NOT_NULL(strstr(r.err, "--verbose"));
}

/* duplicate short option name on same command */
static void _err_duplicate_short_name(void)
{
    Command *root;
    Option  *a, *b;
    root = clp_new_command(0, "prog", NULL);
    a    = clp_new_option("verbose", "v", NULL, TYPE_BOOL, false, false);
    b    = clp_new_option("vverbose", "v", NULL, TYPE_BOOL, false, false);
    clp_add_command_option(root, a);
    clp_add_command_option(root, b);
}
static void test_duplicate_short_option_name_exits(void)
{
    ChildResult r = run_child(_err_duplicate_short_name);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "already registered"));
    D_TEST_NOT_NULL(strstr(r.err, "-v"));
}

/* required operand registered after optional one */
static void _err_required_after_optional_operand(void)
{
    Command *root;
    Operand  opt_op, req_op;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&opt_op, "optional", NULL, TYPE_STR, false);
    clp_init_opnd(&req_op, "required", NULL, TYPE_STR, true);
    clp_add_command_operand(root, &opt_op);
    clp_add_command_operand(root, &req_op);
}
static void test_required_opnd_after_optional_exits(void)
{
    ChildResult r = run_child(_err_required_after_optional_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot follow optional operand"));
    D_TEST_NOT_NULL(strstr(r.err, "required"));
    D_TEST_NOT_NULL(strstr(r.err, "optional"));
}

/* invalid long option name at init: starts with hyphen */
static void _err_long_opt_name_starts_with_hyphen(void)
{
    clp_new_option_raw("-bad", "b", NULL, false, OPT_ACT_SET, (Value){0}, TYPE_BOOL, false, false);
}
static void test_long_opt_name_starting_with_hyphen_exits(void)
{
    ChildResult r = run_child(_err_long_opt_name_starts_with_hyphen);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "a long option must"));
}

/* invalid long option name at init: starts with underscore */
static void _err_long_opt_name_starts_with_underscore(void)
{
    clp_new_option_raw("_bad", "b", NULL, false, OPT_ACT_SET, (Value){0}, TYPE_BOOL, false, false);
}
static void test_long_opt_name_starting_with_underscore_exits(void)
{
    ChildResult r = run_child(_err_long_opt_name_starts_with_underscore);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "a long option must"));
}

/* invalid short option name at init: non-alphanumeric */
static void _err_short_opt_name_invalid(void)
{
    clp_new_option_raw("verbose", "!", NULL, false, OPT_ACT_SET, (Value){0}, TYPE_BOOL, false, false);
}
static void test_short_opt_name_non_alnum_exits(void)
{
    ChildResult r = run_child(_err_short_opt_name_invalid);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "alphanumeric"));
}

/* count action with non-usize type is rejected at init */
static void _err_count_action_with_bool_type(void)
{
    clp_new_option_raw("verbose", "v", NULL, false, OPT_ACT_COUNT, (Value){0}, TYPE_BOOL, false, false);
}
static void test_count_action_with_non_usize_type_exits(void)
{
    ChildResult r = run_child(_err_count_action_with_bool_type);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "action 'count'"));
    D_TEST_NOT_NULL(strstr(r.err, "not valid"));
    D_TEST_NOT_NULL(strstr(r.err, "count action requires"));
}

/* count action wrong type via short-only option (different format string branch) */
static void _err_count_action_short_only_wrong_type(void)
{
    clp_new_option_raw(NULL, "v", NULL, false, OPT_ACT_COUNT, (Value){0}, TYPE_BOOL, false, false);
}
static void test_count_action_short_only_with_non_usize_type_exits(void)
{
    ChildResult r = run_child(_err_count_action_short_only_wrong_type);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "action 'count'"));
    D_TEST_NOT_NULL(strstr(r.err, "not valid"));
    D_TEST_NOT_NULL(strstr(r.err, "count action requires"));
    D_TEST_NOT_NULL(strstr(r.err, "-v"));
}

/* parent-level option is not visible inside subcommand */
static void _err_parent_opt_used_in_subcommand(void)
{
    Command *root, *add;
    Option  *force;
    root  = clp_new_command(0, "prog", NULL);
    add   = clp_new_command(1, "add", NULL);
    force = clp_new_option("force", "f", NULL, TYPE_BOOL, false, false);
    clp_add_command_option(root, force);
    clp_add_command_sub_command(root, add);
    char    *argv[] = {"prog", "add", "--force", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_parent_option_not_visible_in_subcommand_exits(void)
{
    ChildResult r = run_child(_err_parent_opt_used_in_subcommand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "--force"));
    D_TEST_NOT_NULL(strstr(r.err, "add"));
}

/* missing required option inside a subcommand */
static void _err_missing_required_opt_in_subcommand(void)
{
    Command *root, *push;
    Option  *token;
    root  = clp_new_command(0, "prog", NULL);
    push  = clp_new_command(1, "push", NULL);
    token = clp_new_option("token", "t", NULL, TYPE_STR, true, false);
    clp_add_command_option(push, token);
    clp_add_command_sub_command(root, push);
    char    *argv[] = {"prog", "push", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_option_in_subcommand_exits(void)
{
    ChildResult r = run_child(_err_missing_required_opt_in_subcommand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--token"));
    D_TEST_NOT_NULL(strstr(r.err, "push"));
}

/* missing required operand inside a subcommand */
static void _err_missing_required_opnd_in_subcommand(void)
{
    Command *root, *cp;
    Operand  dst;
    root = clp_new_command(0, "prog", NULL);
    cp   = clp_new_command(1, "cp", NULL);
    clp_init_opnd(&dst, "dst", NULL, TYPE_STR, true);
    clp_add_command_operand(cp, &dst);
    clp_add_command_sub_command(root, cp);
    char    *argv[] = {"prog", "cp", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_opnd_in_subcommand_exits(void)
{
    ChildResult r = run_child(_err_missing_required_opnd_in_subcommand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following operands were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "<dst>"));
    D_TEST_NOT_NULL(strstr(r.err, "cp"));
}

/* duplicate operand name emits a warning but does NOT exit */
static void _warn_duplicate_opnd_name(void)
{
    Command *root;
    Operand  a, b;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&a, "file", NULL, TYPE_STR, false);
    clp_init_opnd(&b, "file", NULL, TYPE_STR, false);
    clp_add_command_operand(root, &a);
    clp_add_command_operand(root, &b);
}
static void test_duplicate_opnd_name_warns_no_exit(void)
{
    ChildResult r = run_child(_warn_duplicate_opnd_name);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.err, "warning"));
    D_TEST_NOT_NULL(strstr(r.err, "already used"));
}

/* invalid long option name: contains a space (invalid mid-name char) */
static void _err_long_opt_name_invalid_chars(void)
{
    clp_new_option_raw("bad name", "b", NULL, false, OPT_ACT_SET, (Value){0}, TYPE_BOOL, false, false);
}
static void test_long_opt_name_with_invalid_chars_exits(void)
{
    ChildResult r = run_child(_err_long_opt_name_invalid_chars);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "a long option must"));
}

/* short option requires argument but is the last token (short prefix in msg) */
static void _err_short_opt_no_value(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = clp_new_option("output", "o", NULL, TYPE_STR, false, false);
    clp_add_command_option(root, output);
    char    *argv[] = {"prog", "-o", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_short_opt_missing_value_exits(void)
{
    ChildResult r = run_child(_err_short_opt_no_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "option require an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "-o"));
}

/* invalid value delivered via short form: -j abc where j is TYPE_USIZE */
static void _err_invalid_value_via_short_opt(void)
{
    Command *root;
    Option  *jobs;
    root = clp_new_command(0, "prog", NULL);
    jobs = clp_new_option("jobs", "j", NULL, TYPE_USIZE, false, false);
    clp_add_command_option(root, jobs);
    char    *argv[] = {"prog", "-j", "abc", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_value_via_short_opt_exits(void)
{
    ChildResult r = run_child(_err_invalid_value_via_short_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "abc"));
    D_TEST_NOT_NULL(strstr(r.err, "-j"));
}

/* invalid value for TYPE_DOUBLE option */
static void _err_invalid_double_opt_value(void)
{
    Command *root;
    Option  *rate;
    root = clp_new_command(0, "prog", NULL);
    rate = clp_new_option("rate", "r", NULL, TYPE_DOUBLE, false, false);
    clp_add_command_option(root, rate);
    char    *argv[] = {"prog", "--rate", "notadouble", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_double_option_value_exits(void)
{
    ChildResult r = run_child(_err_invalid_double_opt_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notadouble"));
    D_TEST_NOT_NULL(strstr(r.err, "--rate"));
}

/* invalid value for a TYPE_USIZE operand */
static void _err_invalid_usize_operand(void)
{
    Command *root;
    Operand  count;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&count, "count", NULL, TYPE_USIZE, false);
    clp_add_command_operand(root, &count);
    char    *argv[] = {"prog", "notanumber", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_usize_opnd_value_exits(void)
{
    ChildResult r = run_child(_err_invalid_usize_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notanumber"));
    D_TEST_NOT_NULL(strstr(r.err, "<count>"));
}

/* invalid value for a TYPE_LONG operand */
static void _err_invalid_long_operand(void)
{
    Command *root;
    Operand  offset;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&offset, "offset", NULL, TYPE_LONG, false);
    clp_add_command_operand(root, &offset);
    char    *argv[] = {"prog", "xyz", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_long_opnd_value_exits(void)
{
    ChildResult r = run_child(_err_invalid_long_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "xyz"));
    D_TEST_NOT_NULL(strstr(r.err, "<offset>"));
}

/* invalid value for a TYPE_CHAR operand (too many chars) */
static void _err_invalid_char_operand(void)
{
    Command *root;
    Operand  delim;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&delim, "delim", NULL, TYPE_CHAR, false);
    clp_add_command_operand(root, &delim);
    char    *argv[] = {"prog", "ab", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_char_opnd_value_exits(void)
{
    ChildResult r = run_child(_err_invalid_char_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "too many characters"));
    D_TEST_NOT_NULL(strstr(r.err, "<delim>"));
}

/* invalid value for a TYPE_DOUBLE operand */
static void _err_invalid_double_operand(void)
{
    Command *root;
    Operand  scale;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&scale, "scale", NULL, TYPE_DOUBLE, false);
    clp_add_command_operand(root, &scale);
    char    *argv[] = {"prog", "notdouble", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_double_opnd_value_exits(void)
{
    ChildResult r = run_child(_err_invalid_double_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notdouble"));
    D_TEST_NOT_NULL(strstr(r.err, "<scale>"));
}

/* multiple missing required options: all listed before exit */
static void _err_multiple_missing_required_opts(void)
{
    Command *root;
    Option  *output, *config;
    root   = clp_new_command(0, "prog", NULL);
    output = clp_new_option("output", "o", NULL, TYPE_STR, true, false);
    config = clp_new_option("config", "c", NULL, TYPE_STR, true, false);
    clp_add_command_option(root, output);
    clp_add_command_option(root, config);
    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_multiple_missing_required_options_exits(void)
{
    ChildResult r = run_child(_err_multiple_missing_required_opts);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--output"));
    D_TEST_NOT_NULL(strstr(r.err, "--config"));
}

/* multiple missing required operands: all listed before exit */
static void _err_multiple_missing_required_operands(void)
{
    Command *root;
    Operand  src, dst;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&src, "src", NULL, TYPE_STR, true);
    clp_init_opnd(&dst, "dst", NULL, TYPE_STR, true);
    clp_add_command_operand(root, &src);
    clp_add_command_operand(root, &dst);
    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_multiple_missing_required_operands_exits(void)
{
    ChildResult r = run_child(_err_multiple_missing_required_operands);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following operands were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "<src>"));
    D_TEST_NOT_NULL(strstr(r.err, "<dst>"));
}

/* unknown option in combined short token */
static void _err_unknown_short_in_combined(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", NULL);
    verbose = clp_new_option("verbose", "v", NULL, TYPE_BOOL, false, false);
    clp_add_command_option(root, verbose);
    char    *argv[] = {"prog", "-vz", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_unknown_short_in_combined_token_exits(void)
{
    ChildResult r = run_child(_err_unknown_short_in_combined);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "-z"));
}

/* ── print_usage tests ──────────────────────────────────────────────────── */

/* usage line is printed to stderr when required option is missing (no args) */
static void _usage_missing_required_opt(void)
{
    Command *root;
    Option  *output;
    root   = clp_new_command(0, "prog", NULL);
    output = clp_new_option("output", "o", NULL, TYPE_STR, true, false);
    clp_add_command_option(root, output);
    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_in_stderr_on_missing_required_option(void)
{
    ChildResult r = run_child(_usage_missing_required_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "Usage:"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
}

/* "For more information, try '--help'." is printed alongside the usage line */
static void test_for_more_info_hint_on_missing_required_option(void)
{
    ChildResult r = run_child(_usage_missing_required_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "For more information, try '--help'."));
}

/* usage line is printed to stderr when required operand is missing (no args) */
static void _usage_missing_required_operand(void)
{
    Command *root;
    Operand  file;
    root = clp_new_command(0, "prog", NULL);
    clp_init_opnd(&file, "file", NULL, TYPE_STR, true);
    clp_add_command_operand(root, &file);
    char    *argv[] = {"prog", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_in_stderr_on_missing_required_operand(void)
{
    ChildResult r = run_child(_usage_missing_required_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "Usage:"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
    D_TEST_NOT_NULL(strstr(r.err, "For more information, try '--help'."));
}

/* usage line in --help output contains "Usage:" and the command name */
static void _usage_in_help_output(void)
{
    Command *root;
    root            = clp_new_command(0, "mytool", "a test tool");
    char    *argv[] = {"mytool", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_appears_in_help_stdout(void)
{
    ChildResult r = run_child_stdout(_usage_in_help_output);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Usage:"));
    D_TEST_NOT_NULL(strstr(r.out, "mytool"));
}

/* usage line in --help shows "<COMMAND>" placeholder for commands with subcommands */
static void _usage_help_with_subcommand(void)
{
    Command *root, *sub;
    root = clp_new_command(0, "prog", "test");
    sub  = clp_new_command(1, "sub", "a subcommand");
    clp_add_command_sub_command(root, sub);
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_shows_command_placeholder(void)
{
    ChildResult r = run_child_stdout(_usage_help_with_subcommand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "<COMMAND>"));
}

/* usage line in --help shows operand names for commands with operands */
static void _usage_help_with_operand(void)
{
    Command *root;
    Operand  src, dst;
    root = clp_new_command(0, "prog", "test");
    init_str_operand(&src, "source", false);
    init_str_operand(&dst, "dest", false);
    clp_add_command_operand(root, &src);
    clp_add_command_operand(root, &dst);
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_shows_opnd_names(void)
{
    ChildResult r = run_child_stdout(_usage_help_with_operand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "<source>"));
    D_TEST_NOT_NULL(strstr(r.out, "<dest>"));
}

/* usage line shows "..." ellipsis for list operands */
static void _usage_help_with_list_operand(void)
{
    Command *root;
    Operand  files;
    root = clp_new_command(0, "prog", "test");
    init_list_operand(&files, "files", true);
    clp_add_command_operand(root, &files);
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_shows_ellipsis_for_list_operand(void)
{
    ChildResult r = run_child_stdout(_usage_help_with_list_operand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "<files>..."));
}

/* usage line in --help includes option names */
static void _usage_help_with_options(void)
{
    Command *root;
    Option  *verbose, *output;
    root    = clp_new_command(0, "prog", "test");
    verbose = init_bool_opt("verbose", "v", false);
    output  = init_str_opt("output", "o", false);
    clp_add_command_option(root, verbose);
    clp_add_command_option(root, output);
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_includes_option_names(void)
{
    ChildResult r = run_child_stdout(_usage_help_with_options);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "--verbose"));
    D_TEST_NOT_NULL(strstr(r.out, "--output"));
}

/* ── help / -h built-in ─────────────────────────────────────────────────── */

/* registering "help" as a long option name is rejected (reserved) */
static void _err_register_help_as_long_opt(void)
{
    clp_new_option_raw("help", "H", NULL, false, OPT_ACT_SET, (Value){0}, TYPE_BOOL, false, false);
}
static void test_registering_help_long_opt_exits(void)
{
    ChildResult r = run_child(_err_register_help_as_long_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "reserved option"));
    D_TEST_NOT_NULL(strstr(r.err, "help"));
}

/* --help exits with success and prints description */
static void _help_long_opt_prog(void)
{
    Command *root;
    root            = clp_new_command(0, "prog", "a test program");
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_long_help_exits_success_and_prints_description(void)
{
    ChildResult r = run_child_stdout(_help_long_opt_prog);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Description:"));
    D_TEST_NOT_NULL(strstr(r.out, "a test program"));
}

/* -h exits with success and prints description when no user -h is registered */
static void _help_short_opt_prog(void)
{
    Command *root;
    root            = clp_new_command(0, "prog", "a test program");
    char    *argv[] = {"prog", "-h", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_short_help_exits_success_when_not_user_defined(void)
{
    ChildResult r = run_child_stdout(_help_short_opt_prog);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Description:"));
}

/* -h processes as normal option when user has registered their own -h */
static void test_short_help_not_triggered_when_user_defined_h(void)
{
    Command *root;
    Option  *h;
    root = clp_new_command(0, "prog", "test");
    h    = clp_new_option(NULL, "h", NULL, TYPE_BOOL, false, false);
    clp_add_command_option(root, h);
    char    *argv[] = {"prog", "-h", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(h->value_set == true);
    D_TEST_EXPR(h->value.value_bool == true);
    clp_cleanup(root);
}

/* help output includes Options: section with registered option names */
static void _help_with_options(void)
{
    Command *root;
    Option  *verbose;
    root    = clp_new_command(0, "prog", "a test program");
    verbose = init_bool_opt("verbose", "v", false);
    clp_add_command_option(root, verbose);
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_output_includes_options_section(void)
{
    ChildResult r = run_child_stdout(_help_with_options);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Options:"));
    D_TEST_NOT_NULL(strstr(r.out, "--verbose"));
}

/* help output includes Commands: section with registered subcommand names */
static void _help_with_subcommand(void)
{
    Command *root, *add;
    root = clp_new_command(0, "prog", "a test program");
    add  = clp_new_command(1, "add", "add files");
    clp_add_command_sub_command(root, add);
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_output_includes_commands_section(void)
{
    ChildResult r = run_child_stdout(_help_with_subcommand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Commands:"));
    D_TEST_NOT_NULL(strstr(r.out, "add"));
}

/* help output includes Arguments: section with registered operand names */
static void _help_with_operand(void)
{
    Command *root;
    Operand  file;
    root = clp_new_command(0, "prog", "a test program");
    init_str_operand(&file, "file", false);
    clp_add_command_operand(root, &file);
    char    *argv[] = {"prog", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_output_includes_arguments_section(void)
{
    ChildResult r = run_child_stdout(_help_with_operand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Arguments:"));
    D_TEST_NOT_NULL(strstr(r.out, "<file>"));
}

/* prog subcmd --help shows the subcommand's own help, not root's */
static void _help_on_subcommand(void)
{
    Command *root, *push;
    Option  *force;
    root  = clp_new_command(0, "prog", "root program");
    push  = clp_new_command(1, "push", "push changes");
    force = init_bool_opt("force", "f", false);
    clp_add_command_option(push, force);
    clp_add_command_sub_command(root, push);
    char    *argv[] = {"prog", "push", "--help", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_on_subcommand_shows_subcommand_info(void)
{
    ChildResult r = run_child_stdout(_help_on_subcommand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "push changes"));
    D_TEST_NOT_NULL(strstr(r.out, "--force"));
}

/* ── key=value option tests ─────────────────────────────────────────────── */

static void test_kv_option_single_pair(void)
{
    Command *root;
    Option  *env;
    root = clp_new_command(0, "prog", NULL);
    env  = init_kv_opt("env", "e");
    clp_add_command_option(root, env);

    char    *argv[] = {"prog", "--env", "HOST=localhost", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);

    D_TEST_EXPR(env->value_set == true);
    DStringView  key = D_STRING_VIEW_FROM_LITERAL("HOST");
    DStringView *val = d_unordered_map_get(&env->value.value_kv, &key);
    D_TEST_NOT_NULL(val);
    D_TEST_EXPR(d_string_view_compare_against_c_string(*val, "localhost"));
    clp_cleanup(root);
}

static void test_kv_option_multiple_pairs(void)
{
    Command *root;
    Option  *env;
    root = clp_new_command(0, "prog", NULL);
    env  = init_kv_opt("env", "e");
    clp_add_command_option(root, env);

    char    *argv[] = {"prog", "--env", "HOST=localhost,PORT=8080", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);

    D_TEST_EXPR(env->value_set == true);
    DStringView  k1 = D_STRING_VIEW_FROM_LITERAL("HOST");
    DStringView  k2 = D_STRING_VIEW_FROM_LITERAL("PORT");
    DStringView *v1 = d_unordered_map_get(&env->value.value_kv, &k1);
    DStringView *v2 = d_unordered_map_get(&env->value.value_kv, &k2);
    D_TEST_NOT_NULL(v1);
    D_TEST_NOT_NULL(v2);
    D_TEST_EXPR(d_string_view_compare_against_c_string(*v1, "localhost"));
    D_TEST_EXPR(d_string_view_compare_against_c_string(*v2, "8080"));
    clp_cleanup(root);
}

static void test_kv_operand_single_pair(void)
{
    Command *root;
    Operand  binding;
    root = clp_new_command(0, "prog", NULL);
    init_kv_operand(&binding, "binding", true);
    clp_add_command_operand(root, &binding);

    char    *argv[] = {"prog", "HOST=localhost", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);

    D_TEST_EXPR(binding.value_set == true);
    DStringView  key = D_STRING_VIEW_FROM_LITERAL("HOST");
    DStringView *val = d_unordered_map_get(&binding.value.value_kv, &key);
    D_TEST_NOT_NULL(val);
    D_TEST_EXPR(d_string_view_compare_against_c_string(*val, "localhost"));
    clp_cleanup(root);
}

static void _err_kv_opt_missing_eq(void)
{
    Command *root;
    Option  *env;
    root = clp_new_command(0, "prog", NULL);
    env  = init_kv_opt("env", "e");
    clp_add_command_option(root, env);
    char    *argv[] = {"prog", "--env", "HOSTlocalhost", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opt_missing_eq_exits(void)
{
    ChildResult r = run_child(_err_kv_opt_missing_eq);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "missing '='"));
    D_TEST_NOT_NULL(strstr(r.err, "HOSTlocalhost"));
    D_TEST_NOT_NULL(strstr(r.err, "--env"));
}

static void _err_kv_opt_empty_key(void)
{
    Command *root;
    Option  *env;
    root = clp_new_command(0, "prog", NULL);
    env  = init_kv_opt("env", "e");
    clp_add_command_option(root, env);
    char    *argv[] = {"prog", "--env", "=localhost", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opt_empty_key_exits(void)
{
    ChildResult r = run_child(_err_kv_opt_empty_key);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty key"));
    D_TEST_NOT_NULL(strstr(r.err, "--env"));
}

static void _err_kv_opt_empty_value(void)
{
    Command *root;
    Option  *env;
    root = clp_new_command(0, "prog", NULL);
    env  = init_kv_opt("env", "e");
    clp_add_command_option(root, env);
    char    *argv[] = {"prog", "--env", "HOST=", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opt_empty_value_exits(void)
{
    ChildResult r = run_child(_err_kv_opt_empty_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty value"));
    D_TEST_NOT_NULL(strstr(r.err, "HOST"));
    D_TEST_NOT_NULL(strstr(r.err, "--env"));
}

static void _err_kv_opnd_missing_eq(void)
{
    Command *root;
    Operand  binding;
    root = clp_new_command(0, "prog", NULL);
    init_kv_operand(&binding, "binding", true);
    clp_add_command_operand(root, &binding);
    char    *argv[] = {"prog", "HOSTlocalhost", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opnd_missing_eq_exits(void)
{
    ChildResult r = run_child(_err_kv_opnd_missing_eq);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "missing '='"));
    D_TEST_NOT_NULL(strstr(r.err, "HOSTlocalhost"));
}

static void _err_kv_opnd_empty_key(void)
{
    Command *root;
    Operand  binding;
    root = clp_new_command(0, "prog", NULL);
    init_kv_operand(&binding, "binding", true);
    clp_add_command_operand(root, &binding);
    char    *argv[] = {"prog", "=localhost", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opnd_empty_key_exits(void)
{
    ChildResult r = run_child(_err_kv_opnd_empty_key);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty key"));
}

static void _err_kv_opnd_empty_value(void)
{
    Command *root;
    Operand  binding;
    root = clp_new_command(0, "prog", NULL);
    init_kv_operand(&binding, "binding", true);
    clp_add_command_operand(root, &binding);
    char    *argv[] = {"prog", "HOST=", NULL};
    Command *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opnd_empty_value_exits(void)
{
    ChildResult r = run_child(_err_kv_opnd_empty_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty value"));
    D_TEST_NOT_NULL(strstr(r.err, "HOST"));
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    DTest tests[] = {
        /* getters */
        D_TEST_GENERATE_TEST(test_get_option_by_short_finds_registered_option),
        D_TEST_GENERATE_TEST(test_get_option_by_short_returns_null_for_unknown),
        D_TEST_GENERATE_TEST(test_get_option_by_long_finds_registered_option),
        D_TEST_GENERATE_TEST(test_get_option_by_long_returns_null_for_unknown),
        D_TEST_GENERATE_TEST(test_get_opnd_finds_registered_operand),
        D_TEST_GENERATE_TEST(test_get_opnd_returns_null_for_unknown),
        /* bool flags */
        D_TEST_GENERATE_TEST(test_long_bool_flag_sets_value),
        D_TEST_GENERATE_TEST(test_short_bool_flag_sets_value),
        D_TEST_GENERATE_TEST(test_unprovided_optional_bool_stays_unset),
        D_TEST_GENERATE_TEST(test_combined_short_bool_flags),
        D_TEST_GENERATE_TEST(test_multiple_separate_short_bool_flags),
        D_TEST_GENERATE_TEST(test_combined_flags_only_set_found_options),
        /* non-bool option values */
        D_TEST_GENERATE_TEST(test_short_opt_inline_value),
        D_TEST_GENERATE_TEST(test_short_opt_next_argv_value),
        D_TEST_GENERATE_TEST(test_long_opt_inline_eq_value),
        D_TEST_GENERATE_TEST(test_long_opt_next_argv_value),
        D_TEST_GENERATE_TEST(test_combined_bools_then_inline_value_opt),
        /* count */
        D_TEST_GENERATE_TEST(test_count_option_increments_once),
        D_TEST_GENERATE_TEST(test_count_option_increments_multiple_times),
        D_TEST_GENERATE_TEST(test_count_option_combined_short),
        D_TEST_GENERATE_TEST(test_count_option_mixed_short_and_long),
        /* list options */
        D_TEST_GENERATE_TEST(test_list_option_single_entry),
        D_TEST_GENERATE_TEST(test_list_option_comma_separated),
        /* operands */
        D_TEST_GENERATE_TEST(test_single_opnd_is_set),
        D_TEST_GENERATE_TEST(test_options_and_opnd_together),
        D_TEST_GENERATE_TEST(test_option_after_operand),
        D_TEST_GENERATE_TEST(test_multiple_operands),
        D_TEST_GENERATE_TEST(test_double_hyphen_terminates_option_parsing),
        D_TEST_GENERATE_TEST(test_double_hyphen_with_options_before),
        D_TEST_GENERATE_TEST(test_list_opnd_consumes_remaining_args),
        /* subcommands */
        D_TEST_GENERATE_TEST(test_subcommand_is_dispatched),
        D_TEST_GENERATE_TEST(test_subcommand_with_own_option),
        D_TEST_GENERATE_TEST(test_no_subcommand_leaves_command_null),
        D_TEST_GENERATE_TEST(test_multiple_subcommands_dispatch_correct_one),
        /* type conversions */
        D_TEST_GENERATE_TEST(test_usize_option_parses_decimal),
        D_TEST_GENERATE_TEST(test_long_option_parses_negative),
        D_TEST_GENERATE_TEST(test_char_option_parses_single_char),
        /* edge cases */
        D_TEST_GENERATE_TEST(test_long_opt_empty_inline_value),
        D_TEST_GENERATE_TEST(test_option_interleaved_with_operands),
        D_TEST_GENERATE_TEST(test_bool_option_set_twice_is_ok),
        D_TEST_GENERATE_TEST(test_no_args_with_optional_options),
        D_TEST_GENERATE_TEST(test_long_opt_with_hyphen_in_name),
        /* nested subcommands */
        D_TEST_GENERATE_TEST(test_two_level_subcommand_dispatch),
        D_TEST_GENERATE_TEST(test_three_level_subcommand_dispatch),
        D_TEST_GENERATE_TEST(test_parent_command_pointer_chain),
        D_TEST_GENERATE_TEST(test_root_option_before_subcommand),
        D_TEST_GENERATE_TEST(test_root_option_unset_when_only_sub_option_given),
        D_TEST_GENERATE_TEST(test_options_at_each_of_three_levels),
        D_TEST_GENERATE_TEST(test_sibling_subcommand_options_are_isolated),
        D_TEST_GENERATE_TEST(test_subcommand_with_options_and_operands),
        D_TEST_GENERATE_TEST(test_double_hyphen_in_subcommand_context),
        D_TEST_GENERATE_TEST(test_combined_short_flags_in_subcommand),
        D_TEST_GENERATE_TEST(test_count_option_in_subcommand),
        D_TEST_GENERATE_TEST(test_list_option_in_subcommand),
        D_TEST_GENERATE_TEST(test_long_opt_eq_value_in_subcommand),
        D_TEST_GENERATE_TEST(test_subcommand_with_multiple_operands),
        D_TEST_GENERATE_TEST(test_three_siblings_dispatch_third),
        D_TEST_GENERATE_TEST(test_subcommand_code_is_correct),
        D_TEST_GENERATE_TEST(test_subcommand_short_inline_value_and_operand),
        D_TEST_GENERATE_TEST(test_three_level_options_at_outer_and_inner),
        D_TEST_GENERATE_TEST(test_sibling_registered_after_does_not_bleed),
        D_TEST_GENERATE_TEST(test_root_option_before_two_level_subcommand),
        D_TEST_GENERATE_TEST(test_no_args_with_subcommands_registered),
        D_TEST_GENERATE_TEST(test_subcommand_with_list_operand),
        /* global options inherited from parent to child */
        D_TEST_GENERATE_TEST(test_global_option_set_at_parent_accessible_after_dispatch),
        D_TEST_GENERATE_TEST(test_required_global_option_set_at_parent_satisfies_child),
        D_TEST_GENERATE_TEST(test_required_global_option_unset_exits_via_child),
        D_TEST_GENERATE_TEST(test_non_global_required_option_not_inherited_to_child),
        D_TEST_GENERATE_TEST(test_global_option_inherited_through_nested_levels),
        D_TEST_GENERATE_TEST(test_multiple_global_options_all_inherited),
        /* 5-level deep stress test */
        D_TEST_GENERATE_TEST(test_five_level_deep_with_many_options_and_operands),
        /* error / exit paths */
        D_TEST_GENERATE_TEST(test_unknown_long_option_exits),
        D_TEST_GENERATE_TEST(test_unknown_short_option_exits),
        D_TEST_GENERATE_TEST(test_missing_required_option_exits),
        D_TEST_GENERATE_TEST(test_missing_required_opnd_exits),
        D_TEST_GENERATE_TEST(test_too_many_operands_exits),
        D_TEST_GENERATE_TEST(test_invalid_usize_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_long_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_char_value_too_many_chars_exits),
        D_TEST_GENERATE_TEST(test_invalid_char_value_empty_string_exits),
        D_TEST_GENERATE_TEST(test_bool_opt_with_inline_value_exits),
        D_TEST_GENERATE_TEST(test_count_opt_with_inline_value_exits),
        D_TEST_GENERATE_TEST(test_str_opt_missing_value_exits),
        D_TEST_GENERATE_TEST(test_duplicate_long_option_name_exits),
        D_TEST_GENERATE_TEST(test_duplicate_short_option_name_exits),
        D_TEST_GENERATE_TEST(test_required_opnd_after_optional_exits),
        D_TEST_GENERATE_TEST(test_long_opt_name_starting_with_hyphen_exits),
        D_TEST_GENERATE_TEST(test_long_opt_name_starting_with_underscore_exits),
        D_TEST_GENERATE_TEST(test_short_opt_name_non_alnum_exits),
        D_TEST_GENERATE_TEST(test_count_action_with_non_usize_type_exits),
        D_TEST_GENERATE_TEST(test_count_action_short_only_with_non_usize_type_exits),
        D_TEST_GENERATE_TEST(test_parent_option_not_visible_in_subcommand_exits),
        D_TEST_GENERATE_TEST(test_missing_required_option_in_subcommand_exits),
        D_TEST_GENERATE_TEST(test_missing_required_opnd_in_subcommand_exits),
        D_TEST_GENERATE_TEST(test_unknown_short_in_combined_token_exits),
        D_TEST_GENERATE_TEST(test_duplicate_opnd_name_warns_no_exit),
        D_TEST_GENERATE_TEST(test_long_opt_name_with_invalid_chars_exits),
        D_TEST_GENERATE_TEST(test_add_subcommand_when_operands_exist_exits),
        D_TEST_GENERATE_TEST(test_add_opnd_when_subcommands_exist_exits),
        D_TEST_GENERATE_TEST(test_short_opt_missing_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_value_via_short_opt_exits),
        D_TEST_GENERATE_TEST(test_invalid_double_option_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_usize_opnd_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_long_opnd_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_char_opnd_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_double_opnd_value_exits),
        D_TEST_GENERATE_TEST(test_multiple_missing_required_options_exits),
        D_TEST_GENERATE_TEST(test_multiple_missing_required_operands_exits),
        /* key=value option and operand */
        D_TEST_GENERATE_TEST(test_kv_option_single_pair),
        D_TEST_GENERATE_TEST(test_kv_option_multiple_pairs),
        D_TEST_GENERATE_TEST(test_kv_operand_single_pair),
        D_TEST_GENERATE_TEST(test_kv_opt_missing_eq_exits),
        D_TEST_GENERATE_TEST(test_kv_opt_empty_key_exits),
        D_TEST_GENERATE_TEST(test_kv_opt_empty_value_exits),
        D_TEST_GENERATE_TEST(test_kv_opnd_missing_eq_exits),
        D_TEST_GENERATE_TEST(test_kv_opnd_empty_key_exits),
        D_TEST_GENERATE_TEST(test_kv_opnd_empty_value_exits),
        /* print_usage */
        D_TEST_GENERATE_TEST(test_usage_line_in_stderr_on_missing_required_option),
        D_TEST_GENERATE_TEST(test_for_more_info_hint_on_missing_required_option),
        D_TEST_GENERATE_TEST(test_usage_line_in_stderr_on_missing_required_operand),
        D_TEST_GENERATE_TEST(test_usage_line_appears_in_help_stdout),
        D_TEST_GENERATE_TEST(test_usage_line_shows_command_placeholder),
        D_TEST_GENERATE_TEST(test_usage_line_shows_opnd_names),
        D_TEST_GENERATE_TEST(test_usage_line_shows_ellipsis_for_list_operand),
        D_TEST_GENERATE_TEST(test_usage_line_includes_option_names),
        /* help / -h built-in */
        D_TEST_GENERATE_TEST(test_registering_help_long_opt_exits),
        D_TEST_GENERATE_TEST(test_long_help_exits_success_and_prints_description),
        D_TEST_GENERATE_TEST(test_short_help_exits_success_when_not_user_defined),
        D_TEST_GENERATE_TEST(test_short_help_not_triggered_when_user_defined_h),
        D_TEST_GENERATE_TEST(test_help_output_includes_options_section),
        D_TEST_GENERATE_TEST(test_help_output_includes_commands_section),
        D_TEST_GENERATE_TEST(test_help_output_includes_arguments_section),
        D_TEST_GENERATE_TEST(test_help_on_subcommand_shows_subcommand_info),
    };
    D_TEST_RUN_TESTS(tests);
    return 0;
}
