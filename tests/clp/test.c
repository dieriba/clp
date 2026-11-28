#include "clp.h"
#include "d_test.h"
#include "d_unordered_map.h"

#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdlib.h>

/* ── builder shim: builders return clp_status_t; tests treat failure as fatal ─ */

static void t_die(const clp_error_t *e)
{
    clp_print_error(stderr, e);
    exit(EXIT_FAILURE);
}

#define T_NEW(T, ctor, ...)                                                                                                                                                                            \
    ({                                                                                                                                                                                                 \
        T          *t_out_;                                                                                                                                                                            \
        clp_error_t t_err_;                                                                                                                                                                            \
        if (ctor(&t_out_, __VA_ARGS__, &t_err_) != CLP_OK)                                                                                                                                             \
            t_die(&t_err_);                                                                                                                                                                            \
        t_out_;                                                                                                                                                                                        \
    })

#define T_ADD(fn, ...)                                                                                                                                                                                 \
    ({                                                                                                                                                                                                 \
        clp_error_t t_err_;                                                                                                                                                                            \
        if (fn(__VA_ARGS__, &t_err_) != CLP_OK)                                                                                                                                                        \
            t_die(&t_err_);                                                                                                                                                                            \
    })

/* ── dest storage: clp writes into caller-owned variables ───────────────
 *
 * Tests read parsed values back through TV(x), a test_value_t owned by the test.
 * The t_new_* shims keep the pre-dest argument lists (type / default clp_value_t) so
 * the bulk of the suite reads as before; the real typed macros are covered by
 * the "dest binding" tests further down. */

typedef union test_value_t
{
    d_unordered_map_t value_kv;
    d_dyn_array_t     value_list;
    d_string_view_t   value_d_string_view;
    usize             value_usize;
    double            value_double;
    long              value_long;
    bool              value_bool;
    char              value_char;
} test_value_t;

#define T_MAX_DESTS 1024

static struct
{
    test_value_t value;
    int          kind; /* 0 scalar, 1 list, 2 kv */
} t_dests[T_MAX_DESTS];
static usize t_dests_count;

#define TV(x) (*(test_value_t *) (x)->dest)

static void t_release_dests(void)
{
    for (usize i = 0; i < t_dests_count; i++)
    {
        if (t_dests[i].kind == 1)
            d_dyn_array_destroy(&t_dests[i].value.value_list);
        else if (t_dests[i].kind == 2)
            d_unordered_map_destroy(&t_dests[i].value.value_kv);
    }
    t_dests_count = 0;
}

/* kind: 0 scalar (filled from def), 1 list, 2 kv */
static test_value_t *t_dest(int kind, clp_value_t def)
{
    if (t_dests_count == T_MAX_DESTS)
    {
        fprintf(stderr, "test: T_MAX_DESTS exhausted\n");
        exit(EXIT_FAILURE);
    }
    test_value_t *tv = &t_dests[t_dests_count].value;
    memset(tv, 0, sizeof(*tv));
    t_dests[t_dests_count++].kind = kind;
    if (kind == 1)
    {
        if (d_dyn_array_init(&tv->value_list, sizeof(d_string_view_t), 1, NULL, NULL, RAW_BUF_OPT_NONE) != D_OK)
            exit(EXIT_FAILURE);
    }
    else if (kind == 2)
    {
        if (d_unordered_map_init_not_owned_d_string_view_key(&tv->value_kv, sizeof(d_string_view_t), 1, NULL))
            exit(EXIT_FAILURE);
    }
    else
        memcpy(tv, &def, sizeof(def));
    return tv;
}

static clp_status_t t_new_option_raw(clp_option_t **out, char *ln, char *sn, char *desc, bool has_def, clp_opt_action_t action, clp_value_t def, clp_type_t type, bool req, bool glob,
                                     clp_checker_t checker, clp_error_t *err)
{
    int kind = (!has_def && action == CLP_OPT_ACT_LIST) ? 1 : (!has_def && action == CLP_OPT_ACT_KV) ? 2 : 0;
    return clp_new_option_raw(out, ln, sn, desc, t_dest(kind, def), action, type, req, glob, checker, NULL, err);
}

static clp_status_t t_new_operand_raw(clp_operand_t **out, char *name, char *desc, bool has_def, clp_operand_action_t action, clp_value_t def, clp_type_t type, bool req, clp_checker_t checker,
                                      clp_error_t *err)
{
    int kind = (!has_def && action == CLP_OPERAND_ACT_LIST) ? 1 : (!has_def && action == CLP_OPERAND_ACT_KV) ? 2 : 0;
    return clp_new_operand_raw(out, name, desc, t_dest(kind, def), action, type, req, checker, NULL, err);
}

#define t_new_option(out, ln, sn, desc, type, req, glob, err) t_new_option_raw(out, ln, sn, desc, false, CLP_OPT_ACT_SET, (clp_value_t){0}, type, req, glob, NULL, err)
#define t_new_option_checked(out, ln, sn, desc, type, req, glob, chk, err) t_new_option_raw(out, ln, sn, desc, false, CLP_OPT_ACT_SET, (clp_value_t){0}, type, req, glob, chk, err)
#define t_new_option_count(out, ln, sn, desc, glob, err) t_new_option_raw(out, ln, sn, desc, false, CLP_OPT_ACT_COUNT, (clp_value_t){0}, CLP_TYPE_USIZE, false, glob, NULL, err)
#define t_new_option_list(out, ln, sn, desc, req, glob, err) t_new_option_raw(out, ln, sn, desc, false, CLP_OPT_ACT_LIST, (clp_value_t){0}, CLP_TYPE_STR, req, glob, NULL, err)
#define t_new_option_list_checked(out, ln, sn, desc, req, glob, chk, err) t_new_option_raw(out, ln, sn, desc, false, CLP_OPT_ACT_LIST, (clp_value_t){0}, CLP_TYPE_STR, req, glob, chk, err)
#define t_new_option_kv(out, ln, sn, desc, req, glob, err) t_new_option_raw(out, ln, sn, desc, false, CLP_OPT_ACT_KV, (clp_value_t){0}, CLP_TYPE_STR, req, glob, NULL, err)
#define t_new_option_kv_checked(out, ln, sn, desc, req, glob, chk, err) t_new_option_raw(out, ln, sn, desc, false, CLP_OPT_ACT_KV, (clp_value_t){0}, CLP_TYPE_STR, req, glob, chk, err)
#define t_new_option_default_usize_checked(out, ln, sn, desc, def, req, glob, chk, err)                                                                                                                \
    t_new_option_raw(out, ln, sn, desc, true, CLP_OPT_ACT_SET, (clp_value_t){.value_usize = (def)}, CLP_TYPE_USIZE, req, glob, chk, err)
#define t_new_option_default_str_checked(out, ln, sn, desc, def, req, glob, chk, err)                                                                                                                  \
    t_new_option_raw(out, ln, sn, desc, true, CLP_OPT_ACT_SET, (clp_value_t){.value_d_string_view = d_string_view_from_c_string(def)}, CLP_TYPE_STR, req, glob, chk, err)

#define t_new_operand(out, name, desc, type, req, err) t_new_operand_raw(out, name, desc, false, CLP_OPERAND_ACT_SET, (clp_value_t){0}, type, req, NULL, err)
#define t_new_operand_checked(out, name, desc, type, req, chk, err) t_new_operand_raw(out, name, desc, false, CLP_OPERAND_ACT_SET, (clp_value_t){0}, type, req, chk, err)
#define t_new_operand_default_long_checked(out, name, desc, def, req, chk, err)                                                                                                                        \
    t_new_operand_raw(out, name, desc, true, CLP_OPERAND_ACT_SET, (clp_value_t){.value_long = (def)}, CLP_TYPE_LONG, req, chk, err)
#define t_new_operand_list(out, name, desc, req, err) t_new_operand_raw(out, name, desc, false, CLP_OPERAND_ACT_LIST, (clp_value_t){0}, CLP_TYPE_STR, req, NULL, err)
#define t_new_operand_list_checked(out, name, desc, req, chk, err) t_new_operand_raw(out, name, desc, false, CLP_OPERAND_ACT_LIST, (clp_value_t){0}, CLP_TYPE_STR, req, chk, err)
#define t_new_operand_kv(out, name, desc, req, err) t_new_operand_raw(out, name, desc, false, CLP_OPERAND_ACT_KV, (clp_value_t){0}, CLP_TYPE_STR, req, NULL, err)
#define t_new_operand_kv_checked(out, name, desc, req, chk, err) t_new_operand_raw(out, name, desc, false, CLP_OPERAND_ACT_KV, (clp_value_t){0}, CLP_TYPE_STR, req, chk, err)

/* ── helpers ────────────────────────────────────────────────────────────── */

static clp_option_t *init_bool_opt(char *lng, char *sht, bool required)
{
    return T_NEW(clp_option_t, t_new_option, lng, sht, NULL, CLP_TYPE_BOOL, required, false);
}

static clp_option_t *init_str_opt(char *lng, char *sht, bool required)
{
    return T_NEW(clp_option_t, t_new_option, lng, sht, NULL, CLP_TYPE_STR, required, false);
}

static clp_option_t *init_count_opt(char *lng, char *sht)
{
    return T_NEW(clp_option_t, t_new_option_count, lng, sht, NULL, false);
}

static clp_option_t *init_list_opt(char *lng, char *sht)
{
    return T_NEW(clp_option_t, t_new_option_list, lng, sht, NULL, false, false);
}

static clp_operand_t *init_str_operand(char *name, bool required)
{
    return T_NEW(clp_operand_t, t_new_operand, name, NULL, CLP_TYPE_STR, required);
}

static clp_operand_t *init_list_operand(char *name, bool required)
{
    return T_NEW(clp_operand_t, t_new_operand_list, name, NULL, required);
}

static clp_option_t *init_kv_opt(char *lng, char *sht)
{
    return T_NEW(clp_option_t, t_new_option_kv, lng, sht, NULL, false, false);
}

static clp_operand_t *init_kv_operand(char *name, bool required)
{
    return T_NEW(clp_operand_t, t_new_operand_kv, name, NULL, required);
}

/* ── fork/pipe helper (used by null-guard and error tests) ──────────────── */

typedef struct
{
    int  status;
    char err[512];
    char out[1024];
} child_result_t;

static child_result_t run_child(void (*fn)(void))
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
    child_result_t r     = {0};
    ssize_t        total = 0, n;
    while ((n = read(pfd[0], r.err + total, sizeof(r.err) - 1 - total)) > 0)
        total += n;
    r.err[total] = '\0';
    close(pfd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

static child_result_t run_child_stdout(void (*fn)(void))
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
    child_result_t r     = {0};
    ssize_t        total = 0, n;
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
    clp_command_t *cmd;
    clp_option_t  *opt;
    cmd = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    opt = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, cmd, opt);
    D_TEST_EXPR(clp_get_option_by_short(cmd, 'v') == opt);
    clp_cleanup(cmd);
}

static void test_get_option_by_short_returns_null_for_unknown(void)
{
    clp_command_t *cmd;
    clp_option_t  *opt;
    cmd = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    opt = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, cmd, opt);
    D_TEST_NULL(clp_get_option_by_short(cmd, 'x'));
    clp_cleanup(cmd);
}

static void test_get_option_by_long_finds_registered_option(void)
{
    clp_command_t *cmd;
    clp_option_t  *opt;
    cmd = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    opt = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, cmd, opt);
    D_TEST_EXPR(clp_get_option_by_long(cmd, D_STRING_VIEW_FROM_LITERAL("verbose")) == opt);
    clp_cleanup(cmd);
}

static void test_get_option_by_long_returns_null_for_unknown(void)
{
    clp_command_t *cmd;
    clp_option_t  *opt;
    cmd = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    opt = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, cmd, opt);
    D_TEST_NULL(clp_get_option_by_long(cmd, D_STRING_VIEW_FROM_LITERAL("quiet")));
    clp_cleanup(cmd);
}

static void test_get_operand_finds_registered_operand(void)
{
    clp_command_t *cmd;
    clp_operand_t *op;
    cmd = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    op  = init_str_operand("file", false);
    T_ADD(clp_add_command_operand, cmd, op);
    D_TEST_EXPR(clp_get_operand(cmd, D_STRING_VIEW_FROM_LITERAL("file")) == op);
    clp_cleanup(cmd);
}

static void test_get_operand_returns_null_for_unknown(void)
{
    clp_command_t *cmd;
    clp_operand_t *op;
    cmd = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    op  = init_str_operand("file", false);
    T_ADD(clp_add_command_operand, cmd, op);
    D_TEST_NULL(clp_get_operand(cmd, D_STRING_VIEW_FROM_LITERAL("output")));
    clp_cleanup(cmd);
}

/* ── bool option parsing ────────────────────────────────────────────────── */

static void test_long_bool_flag_sets_value(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", "--verbose", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(TV(verbose).value_bool == true);
    clp_cleanup(root);
}

static void test_short_bool_flag_sets_value(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", "-v", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(TV(verbose).value_bool == true);
    clp_cleanup(root);
}

static void test_unprovided_optional_bool_stays_unset(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == false);
    D_TEST_EXPR(TV(verbose).value_bool == false);
    clp_cleanup(root);
}

/* POSIX: multiple bool flags combined in one token: -abc */
static void test_combined_short_bool_flags(void)
{
    clp_command_t *root;
    clp_option_t  *fa, *fb, *fc;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    fa   = init_bool_opt("aaa", "a", false);
    fb   = init_bool_opt("bbb", "b", false);
    fc   = init_bool_opt("ccc", "c", false);
    T_ADD(clp_add_command_option, root, fa);
    T_ADD(clp_add_command_option, root, fb);
    T_ADD(clp_add_command_option, root, fc);

    char          *argv[] = {"prog", "-abc", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == true);
    D_TEST_EXPR(fc->value_set == true);
    D_TEST_EXPR(TV(fa).value_bool == true);
    D_TEST_EXPR(TV(fb).value_bool == true);
    D_TEST_EXPR(TV(fc).value_bool == true);
    clp_cleanup(root);
}

/* Parsing multiple separate bool flags */
static void test_multiple_separate_short_bool_flags(void)
{
    clp_command_t *root;
    clp_option_t  *fa, *fb;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    fa   = init_bool_opt("aaa", "a", false);
    fb   = init_bool_opt("bbb", "b", false);
    T_ADD(clp_add_command_option, root, fa);
    T_ADD(clp_add_command_option, root, fb);

    char          *argv[] = {"prog", "-a", "-b", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == true);
    clp_cleanup(root);
}

/* Only first flag in combined token is set when second is unknown */
static void test_combined_flags_only_set_found_options(void)
{
    clp_command_t *root;
    clp_option_t  *fa, *fb;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    fa   = init_bool_opt("aaa", "a", false);
    fb   = init_bool_opt("bbb", "b", false);
    T_ADD(clp_add_command_option, root, fa);
    T_ADD(clp_add_command_option, root, fb);

    char          *argv[] = {"prog", "-a", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == false);
    clp_cleanup(root);
}

/* ── non-bool options: value_set and value correctness ─────────────────── */

/* POSIX: -oVALUE (value is remainder of token, no space) */
static void test_short_opt_inline_value(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = init_str_opt("output", "o", false);
    T_ADD(clp_add_command_option, root, output);

    char          *argv[] = {"prog", "-ofile.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(TV(output).value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* POSIX: -o VALUE (value is next argv token) */
static void test_short_opt_next_argv_value(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = init_str_opt("output", "o", false);
    T_ADD(clp_add_command_option, root, output);

    char          *argv[] = {"prog", "-o", "file.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(TV(output).value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* GNU: --output=VALUE */
static void test_long_opt_inline_eq_value(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = init_str_opt("output", "o", false);
    T_ADD(clp_add_command_option, root, output);

    char          *argv[] = {"prog", "--output=file.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(TV(output).value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* GNU: --output VALUE */
static void test_long_opt_next_argv_value(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = init_str_opt("output", "o", false);
    T_ADD(clp_add_command_option, root, output);

    char          *argv[] = {"prog", "--output", "file.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(TV(output).value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* POSIX: -abVALUE — combined bools followed by value-taking option inline */
static void test_combined_bools_then_inline_value_opt(void)
{
    clp_command_t *root;
    clp_option_t  *fa, *fb, *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    fa     = init_bool_opt("aaa", "a", false);
    fb     = init_bool_opt("bbb", "b", false);
    output = init_str_opt("output", "o", false);
    T_ADD(clp_add_command_option, root, fa);
    T_ADD(clp_add_command_option, root, fb);
    T_ADD(clp_add_command_option, root, output);

    char          *argv[] = {"prog", "-abofile.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(fa->value_set == true);
    D_TEST_EXPR(fb->value_set == true);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(TV(output).value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* ── count options ──────────────────────────────────────────────────────── */

static void test_count_option_increments_once(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_count_opt("verbose", "v");
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", "-v", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(TV(verbose).value_usize == 1);
    clp_cleanup(root);
}

static void test_count_option_increments_multiple_times(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_count_opt("verbose", "v");
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", "-v", "-v", "-v", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(TV(verbose).value_usize == 3);
    clp_cleanup(root);
}

/* POSIX: -vvv combined should count 3 */
static void test_count_option_combined_short(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_count_opt("verbose", "v");
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", "-vvv", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(TV(verbose).value_usize == 3);
    clp_cleanup(root);
}

static void test_count_option_mixed_short_and_long(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_count_opt("verbose", "v");
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", "-v", "--verbose", "-vv", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(TV(verbose).value_usize == 4);
    clp_cleanup(root);
}

/* ── list options ───────────────────────────────────────────────────────── */

static void test_list_option_single_entry(void)
{
    clp_command_t *root;
    clp_option_t  *files;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    files = init_list_opt("files", "f");
    T_ADD(clp_add_command_option, root, files);

    char          *argv[] = {"prog", "--files", "a.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(files->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(files).value_list) == 1);
    clp_cleanup(root);
}

static void test_list_option_comma_separated(void)
{
    clp_command_t *root;
    clp_option_t  *files;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    files = init_list_opt("files", "f");
    T_ADD(clp_add_command_option, root, files);

    char          *argv[] = {"prog", "--files", "a.txt,b.txt,c.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(files->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(files).value_list) == 3);

    d_string_view_t tok;
    D_TEST_EXPR(d_dyn_array_get_elem_at(&TV(files).value_list, 0, &tok) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(tok, "a.txt"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&TV(files).value_list, 1, &tok) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(tok, "b.txt"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&TV(files).value_list, 2, &tok) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(tok, "c.txt"));
    clp_cleanup(root);
}

/* ── operand parsing ────────────────────────────────────────────────────── */

static void test_single_operand_is_set(void)
{
    clp_command_t *root;
    clp_operand_t *file;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    file = init_str_operand("file", false);
    T_ADD(clp_add_command_operand, root, file);

    char          *argv[] = {"prog", "input.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(file->value_set == true);
    D_TEST_STR_EQ(TV(file).value_d_string_view.data, "input.txt");
    clp_cleanup(root);
}

static void test_options_and_operand_together(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    clp_operand_t *file;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    file    = init_str_operand("file", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_operand, root, file);

    char          *argv[] = {"prog", "-v", "input.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(file->value_set == true);
    clp_cleanup(root);
}

/* GNU: options can appear after operands (non-POSIX scanning) */
static void test_option_after_operand(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    clp_operand_t *file;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    file    = init_str_operand("file", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_operand, root, file);

    char          *argv[] = {"prog", "input.txt", "-v", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(file->value_set == true);
    D_TEST_EXPR(verbose->value_set == true);
    clp_cleanup(root);
}

static void test_multiple_operands(void)
{
    clp_command_t *root;
    clp_operand_t *src, *dst;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    src  = init_str_operand("src", false);
    dst  = init_str_operand("dst", false);
    T_ADD(clp_add_command_operand, root, src);
    T_ADD(clp_add_command_operand, root, dst);

    char          *argv[] = {"prog", "a.txt", "b.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(src->value_set == true);
    D_TEST_EXPR(dst->value_set == true);
    D_TEST_STR_EQ(TV(src).value_d_string_view.data, "a.txt");
    D_TEST_STR_EQ(TV(dst).value_d_string_view.data, "b.txt");
    clp_cleanup(root);
}

/* POSIX/GNU: -- terminates option parsing; everything after is an operand */
static void test_double_hyphen_terminates_option_parsing(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    clp_operand_t *file;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    file    = init_str_operand("file", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_operand, root, file);

    char          *argv[] = {"prog", "--", "--verbose", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == false);
    D_TEST_EXPR(file->value_set == true);
    D_TEST_STR_EQ(TV(file).value_d_string_view.data, "--verbose");
    clp_cleanup(root);
}

static void test_double_hyphen_with_options_before(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    clp_operand_t *file;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    file    = init_str_operand("file", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_operand, root, file);

    char          *argv[] = {"prog", "-v", "--", "file.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(file->value_set == true);
    D_TEST_STR_EQ(TV(file).value_d_string_view.data, "file.txt");
    clp_cleanup(root);
}

/* ── list operands ──────────────────────────────────────────────────────── */

static void test_list_operand_consumes_remaining_args(void)
{
    clp_command_t *root;
    clp_operand_t *files;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    files = init_list_operand("files", false);
    T_ADD(clp_add_command_operand, root, files);

    char          *argv[] = {"prog", "a.txt", "b.txt", "c.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(files->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(files).value_list) == 3);
    clp_cleanup(root);
}

/* ── subcommand dispatch ────────────────────────────────────────────────── */

static void test_subcommand_is_dispatched(void)
{
    clp_command_t *root, *add;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add  = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, add);

    char          *argv[] = {"prog", "add", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    clp_cleanup(root);
}

static void test_subcommand_with_own_option(void)
{
    clp_command_t *root, *commit;
    clp_option_t  *msg;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    commit = T_NEW(clp_command_t, clp_new_command, 1, "commit", NULL, NULL, NULL);
    msg    = init_bool_opt("amend", "a", false);
    T_ADD(clp_add_command_option, commit, msg);
    T_ADD(clp_add_command_sub_command, root, commit);

    char          *argv[] = {"prog", "commit", "--amend", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == commit);
    D_TEST_EXPR(msg->value_set == true);
    D_TEST_EXPR(TV(msg).value_bool == true);
    clp_cleanup(root);
}

static void test_no_subcommand_leaves_command_null(void)
{
    clp_command_t *root, *add;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add  = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, add);

    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_NULL(cmd);
    clp_cleanup(root);
}

static void test_multiple_subcommands_dispatch_correct_one(void)
{
    clp_command_t *root, *add, *rm;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add  = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    rm   = T_NEW(clp_command_t, clp_new_command, 2, "rm", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, add);
    T_ADD(clp_add_command_sub_command, root, rm);

    char          *argv[] = {"prog", "rm", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == rm);
    clp_cleanup(root);
}

/* ── type conversions ───────────────────────────────────────────────────── */

static void test_usize_option_parses_decimal(void)
{
    clp_command_t *root;
    clp_option_t  *jobs;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    jobs = T_NEW(clp_option_t, t_new_option, "jobs", "j", NULL, CLP_TYPE_USIZE, false, false);
    T_ADD(clp_add_command_option, root, jobs);

    char          *argv[] = {"prog", "--jobs", "4", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(jobs->value_set == true);
    D_TEST_EXPR(TV(jobs).value_usize == 4);
    clp_cleanup(root);
}

static void test_long_option_parses_negative(void)
{
    clp_command_t *root;
    clp_option_t  *level;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    level = T_NEW(clp_option_t, t_new_option, "level", "l", NULL, CLP_TYPE_LONG, false, false);
    T_ADD(clp_add_command_option, root, level);

    char          *argv[] = {"prog", "--level", "-5", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(level->value_set == true);
    D_TEST_EXPR(TV(level).value_long == -5);
    clp_cleanup(root);
}

static void test_char_option_parses_single_char(void)
{
    clp_command_t *root;
    clp_option_t  *sep;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    sep  = T_NEW(clp_option_t, t_new_option, "sep", "s", NULL, CLP_TYPE_CHAR, false, false);
    T_ADD(clp_add_command_option, root, sep);

    char          *argv[] = {"prog", "--sep", ",", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(sep->value_set == true);
    D_TEST_EXPR(TV(sep).value_char == ',');
    clp_cleanup(root);
}

/* ── edge cases ─────────────────────────────────────────────────────────── */

/* --output= (empty value after =) */
static void test_long_opt_empty_inline_value(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = init_str_opt("output", "o", false);
    T_ADD(clp_add_command_option, root, output);

    char          *argv[] = {"prog", "--output=", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(TV(output).value_d_string_view.data, "");
    clp_cleanup(root);
}

/* options mixed before and after operands (GNU scanning) */
static void test_option_interleaved_with_operands(void)
{
    clp_command_t *root;
    clp_option_t  *verbose, *dry;
    clp_operand_t *src, *dst;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    dry     = init_bool_opt("dry-run", "n", false);
    src     = init_str_operand("src", false);
    dst     = init_str_operand("dst", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_option, root, dry);
    T_ADD(clp_add_command_operand, root, src);
    T_ADD(clp_add_command_operand, root, dst);

    char          *argv[] = {"prog", "src.txt", "-v", "dst.txt", "-n", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(dry->value_set == true);
    D_TEST_EXPR(src->value_set == true);
    D_TEST_EXPR(dst->value_set == true);
    clp_cleanup(root);
}

/* bool option can be set multiple times (not OPT_ACT_SET_UNIQUE) */
static void test_bool_option_set_twice_is_ok(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, root, verbose);

    char          *argv[] = {"prog", "-v", "--verbose", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(TV(verbose).value_bool == true);
    clp_cleanup(root);
}

/* no-arg invocation with only optional options — parses cleanly */
static void test_no_args_with_optional_options(void)
{
    clp_command_t *root;
    clp_option_t  *a, *b;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    a    = init_bool_opt("aaa", "a", false);
    b    = init_bool_opt("bbb", "b", false);
    T_ADD(clp_add_command_option, root, a);
    T_ADD(clp_add_command_option, root, b);

    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(a->value_set == false);
    D_TEST_EXPR(b->value_set == false);
    clp_cleanup(root);
}

/* long option with hyphen in name: --dry-run */
static void test_long_opt_with_hyphen_in_name(void)
{
    clp_command_t *root;
    clp_option_t  *dry;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    dry  = init_bool_opt("dry-run", "n", false);
    T_ADD(clp_add_command_option, root, dry);

    char          *argv[] = {"prog", "--dry-run", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(dry->value_set == true);
    clp_cleanup(root);
}

/* ── nested subcommands ─────────────────────────────────────────────────── */

/* prog cmd subcmd — 2-level dispatch, returned command is innermost */
static void test_two_level_subcommand_dispatch(void)
{
    clp_command_t *root, *remote, *add;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    remote = T_NEW(clp_command_t, clp_new_command, 1, "remote", NULL, NULL, NULL);
    add    = T_NEW(clp_command_t, clp_new_command, 2, "add", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, remote);
    T_ADD(clp_add_command_sub_command, remote, add);

    char          *argv[] = {"prog", "remote", "add", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    clp_cleanup(root);
}

/* prog cmd subcmd subsubcmd — 3-level dispatch */
static void test_three_level_subcommand_dispatch(void)
{
    clp_command_t *root, *remote, *add, *origin;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    remote = T_NEW(clp_command_t, clp_new_command, 1, "remote", NULL, NULL, NULL);
    add    = T_NEW(clp_command_t, clp_new_command, 2, "add", NULL, NULL, NULL);
    origin = T_NEW(clp_command_t, clp_new_command, 3, "origin", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, remote);
    T_ADD(clp_add_command_sub_command, remote, add);
    T_ADD(clp_add_command_sub_command, add, origin);

    char          *argv[] = {"prog", "remote", "add", "origin", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == origin);
    clp_cleanup(root);
}

/* parent_command pointer is set correctly for each level */
static void test_parent_command_pointer_chain(void)
{
    clp_command_t *root, *lvl1, *lvl2;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    lvl1 = T_NEW(clp_command_t, clp_new_command, 1, "cmd", NULL, NULL, NULL);
    lvl2 = T_NEW(clp_command_t, clp_new_command, 2, "sub", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, lvl1);
    T_ADD(clp_add_command_sub_command, lvl1, lvl2);

    D_TEST_EXPR(lvl1->parent_command == root);
    D_TEST_EXPR(lvl2->parent_command == lvl1);
    clp_cleanup(root);
}

/* root option before subcommand is parsed at root scope */
static void test_root_option_before_subcommand(void)
{
    clp_command_t *root, *push;
    clp_option_t  *verbose, *force;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push    = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    force   = init_bool_opt("force", "f", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_option, push, force);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "--verbose", "push", "--force", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* root option is unset when subcommand option is the only thing provided */
static void test_root_option_unset_when_only_sub_option_given(void)
{
    clp_command_t *root, *push;
    clp_option_t  *verbose, *force;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push    = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    force   = init_bool_opt("force", "f", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_option, push, force);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "push", "--force", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(verbose->value_set == false);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* options at all 3 levels each get correctly assigned */
static void test_options_at_each_of_three_levels(void)
{
    clp_command_t *root, *remote, *add;
    clp_option_t  *verbose, *url, *force;
    clp_operand_t *name;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    remote  = T_NEW(clp_command_t, clp_new_command, 1, "remote", NULL, NULL, NULL);
    add     = T_NEW(clp_command_t, clp_new_command, 2, "add", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    url     = init_str_opt("url", "u", false);
    force   = init_bool_opt("force", "f", false);
    name    = init_str_operand("name", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_option, remote, url);
    T_ADD(clp_add_command_option, add, force);
    T_ADD(clp_add_command_operand, add, name);
    T_ADD(clp_add_command_sub_command, root, remote);
    T_ADD(clp_add_command_sub_command, remote, add);

    char          *argv[] = {"prog", "--verbose", "remote", "--url", "git@x.com", "add", "--force", "origin", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(url->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    D_TEST_EXPR(name->value_set == true);
    D_TEST_STR_EQ(TV(url).value_d_string_view.data, "git@x.com");
    D_TEST_STR_EQ(TV(name).value_d_string_view.data, "origin");
    clp_cleanup(root);
}

/* sibling subcommands are isolated — parsing one doesn't affect the other */
static void test_sibling_subcommand_options_are_isolated(void)
{
    clp_command_t *root, *add, *rm;
    clp_option_t  *force, *recursive;
    root      = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add       = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    rm        = T_NEW(clp_command_t, clp_new_command, 2, "rm", NULL, NULL, NULL);
    force     = init_bool_opt("force", "f", false);
    recursive = init_bool_opt("recursive", "r", false);
    T_ADD(clp_add_command_option, add, force);
    T_ADD(clp_add_command_option, rm, recursive);
    T_ADD(clp_add_command_sub_command, root, add);
    T_ADD(clp_add_command_sub_command, root, rm);

    char          *argv[] = {"prog", "rm", "--recursive", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == rm);
    D_TEST_EXPR(recursive->value_set == true);
    D_TEST_EXPR(force->value_set == false);
    clp_cleanup(root);
}

/* subcommand with both options and operands */
static void test_subcommand_with_options_and_operands(void)
{
    clp_command_t *root, *commit;
    clp_option_t  *amend;
    clp_operand_t *msg;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    commit = T_NEW(clp_command_t, clp_new_command, 1, "commit", NULL, NULL, NULL);
    amend  = init_bool_opt("amend", "a", false);
    msg    = init_str_operand("message", false);
    T_ADD(clp_add_command_option, commit, amend);
    T_ADD(clp_add_command_operand, commit, msg);
    T_ADD(clp_add_command_sub_command, root, commit);

    char          *argv[] = {"prog", "commit", "--amend", "my message", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == commit);
    D_TEST_EXPR(amend->value_set == true);
    D_TEST_EXPR(msg->value_set == true);
    D_TEST_STR_EQ(TV(msg).value_d_string_view.data, "my message");
    clp_cleanup(root);
}

/* adding a subcommand to a command that already has operands is rejected */
static void _err_add_sub_when_operands_exist(void)
{
    clp_command_t *root, *push;
    clp_operand_t *file;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    file = init_str_operand("file", false);
    T_ADD(clp_add_command_operand, root, file);
    T_ADD(clp_add_command_sub_command, root, push);
}
static void test_add_subcommand_when_operands_exist_exits(void)
{
    child_result_t r = run_child(_err_add_sub_when_operands_exist);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot have both operands and subcommands"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
}

/* adding an operand to a command that already has subcommands is rejected */
static void _err_add_operand_when_subcommands_exist(void)
{
    clp_command_t *root, *push;
    clp_operand_t *file;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    file = init_str_operand("file", false);
    T_ADD(clp_add_command_sub_command, root, push);
    T_ADD(clp_add_command_operand, root, file);
}
static void test_add_operand_when_subcommands_exist_exits(void)
{
    child_result_t r = run_child(_err_add_operand_when_subcommands_exist);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot have both operands and subcommands"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
}

/* -- in subcommand context terminates its option parsing */
static void test_double_hyphen_in_subcommand_context(void)
{
    clp_command_t *root, *add;
    clp_option_t  *force;
    clp_operand_t *file;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add   = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    force = init_bool_opt("force", "f", false);
    file  = init_str_operand("file", false);
    T_ADD(clp_add_command_option, add, force);
    T_ADD(clp_add_command_operand, add, file);
    T_ADD(clp_add_command_sub_command, root, add);

    char          *argv[] = {"prog", "add", "--", "--force", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(force->value_set == false);
    D_TEST_EXPR(file->value_set == true);
    D_TEST_STR_EQ(TV(file).value_d_string_view.data, "--force");
    clp_cleanup(root);
}

/* combined short flags in subcommand context */
static void test_combined_short_flags_in_subcommand(void)
{
    clp_command_t *root, *add;
    clp_option_t  *recurse, *force;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add     = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    recurse = init_bool_opt("recursive", "r", false);
    force   = init_bool_opt("force", "f", false);
    T_ADD(clp_add_command_option, add, recurse);
    T_ADD(clp_add_command_option, add, force);
    T_ADD(clp_add_command_sub_command, root, add);

    char          *argv[] = {"prog", "add", "-rf", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(recurse->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* count option in subcommand context */
static void test_count_option_in_subcommand(void)
{
    clp_command_t *root, *run;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    run     = T_NEW(clp_command_t, clp_new_command, 1, "run", NULL, NULL, NULL);
    verbose = init_count_opt("verbose", "v");
    T_ADD(clp_add_command_option, run, verbose);
    T_ADD(clp_add_command_sub_command, root, run);

    char          *argv[] = {"prog", "run", "-vvv", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == run);
    D_TEST_EXPR(TV(verbose).value_usize == 3);
    clp_cleanup(root);
}

/* list option in subcommand context */
static void test_list_option_in_subcommand(void)
{
    clp_command_t *root, *build;
    clp_option_t  *features;
    root     = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    build    = T_NEW(clp_command_t, clp_new_command, 1, "build", NULL, NULL, NULL);
    features = init_list_opt("features", "F");
    T_ADD(clp_add_command_option, build, features);
    T_ADD(clp_add_command_sub_command, root, build);

    char          *argv[] = {"prog", "build", "--features", "x,y,z", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == build);
    D_TEST_EXPR(features->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(features).value_list) == 3);
    clp_cleanup(root);
}

/* long opt with = value in subcommand context */
static void test_long_opt_eq_value_in_subcommand(void)
{
    clp_command_t *root, *clone;
    clp_option_t  *depth;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clone = T_NEW(clp_command_t, clp_new_command, 1, "clone", NULL, NULL, NULL);
    depth = T_NEW(clp_option_t, t_new_option, "depth", "d", NULL, CLP_TYPE_USIZE, false, false);
    T_ADD(clp_add_command_option, clone, depth);
    T_ADD(clp_add_command_sub_command, root, clone);

    char          *argv[] = {"prog", "clone", "--depth=1", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == clone);
    D_TEST_EXPR(depth->value_set == true);
    D_TEST_EXPR(TV(depth).value_usize == 1);
    clp_cleanup(root);
}

/* subcommand with multiple operands, all set */
static void test_subcommand_with_multiple_operands(void)
{
    clp_command_t *root, *cp;
    clp_operand_t *src, *dst;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    cp   = T_NEW(clp_command_t, clp_new_command, 1, "cp", NULL, NULL, NULL);
    src  = init_str_operand("src", false);
    dst  = init_str_operand("dst", false);
    T_ADD(clp_add_command_operand, cp, src);
    T_ADD(clp_add_command_operand, cp, dst);
    T_ADD(clp_add_command_sub_command, root, cp);

    char          *argv[] = {"prog", "cp", "a.txt", "b.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == cp);
    D_TEST_EXPR(src->value_set == true);
    D_TEST_EXPR(dst->value_set == true);
    D_TEST_STR_EQ(TV(src).value_d_string_view.data, "a.txt");
    D_TEST_STR_EQ(TV(dst).value_d_string_view.data, "b.txt");
    clp_cleanup(root);
}

/* three sibling subcommands, third one dispatched */
static void test_three_siblings_dispatch_third(void)
{
    clp_command_t *root, *add, *rm, *ls;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add  = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    rm   = T_NEW(clp_command_t, clp_new_command, 2, "rm", NULL, NULL, NULL);
    ls   = T_NEW(clp_command_t, clp_new_command, 3, "ls", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, add);
    T_ADD(clp_add_command_sub_command, root, rm);
    T_ADD(clp_add_command_sub_command, root, ls);

    char          *argv[] = {"prog", "ls", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == ls);
    clp_cleanup(root);
}

/* subcommand code is correct for identification */
static void test_subcommand_code_is_correct(void)
{
    clp_command_t *root, *add, *rm;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add  = T_NEW(clp_command_t, clp_new_command, 42, "add", NULL, NULL, NULL);
    rm   = T_NEW(clp_command_t, clp_new_command, 99, "rm", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, add);
    T_ADD(clp_add_command_sub_command, root, rm);

    char          *argv[] = {"prog", "rm", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd->code == 99);
    clp_cleanup(root);
}

/* subcommand with inline short value and its own operand */
static void test_subcommand_short_inline_value_and_operand(void)
{
    clp_command_t *root, *push;
    clp_option_t  *remote;
    clp_operand_t *branch;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push   = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    remote = init_str_opt("remote", "r", false);
    branch = init_str_operand("branch", false);
    T_ADD(clp_add_command_option, push, remote);
    T_ADD(clp_add_command_operand, push, branch);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "push", "-rorigin", "main", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(remote->value_set == true);
    D_TEST_EXPR(branch->value_set == true);
    D_TEST_STR_EQ(TV(remote).value_d_string_view.data, "origin");
    D_TEST_STR_EQ(TV(branch).value_d_string_view.data, "main");
    clp_cleanup(root);
}

/* deep 3-level: options at level 1 and 3, not level 2 */
static void test_three_level_options_at_outer_and_inner(void)
{
    clp_command_t *root, *grp, *cmd;
    clp_option_t  *quiet, *debug;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    grp   = T_NEW(clp_command_t, clp_new_command, 1, "grp", NULL, NULL, NULL);
    cmd   = T_NEW(clp_command_t, clp_new_command, 2, "cmd", NULL, NULL, NULL);
    quiet = init_bool_opt("quiet", "q", false);
    debug = init_bool_opt("debug", "d", false);
    T_ADD(clp_add_command_option, root, quiet);
    T_ADD(clp_add_command_option, cmd, debug);
    T_ADD(clp_add_command_sub_command, root, grp);
    T_ADD(clp_add_command_sub_command, grp, cmd);

    char          *argv[]     = {"prog", "-q", "grp", "cmd", "--debug", NULL};
    clp_command_t *dispatched = NULL;
    clp_parse_args(root, argv, &dispatched);
    D_TEST_EXPR(dispatched == cmd);
    D_TEST_EXPR(quiet->value_set == true);
    D_TEST_EXPR(debug->value_set == true);
    clp_cleanup(root);
}

/* subcommand options don't bleed into sibling that was registered later */
static void test_sibling_registered_after_does_not_bleed(void)
{
    clp_command_t *root, *fetch, *pull;
    clp_option_t  *all_fetch, *rebase;
    root      = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    fetch     = T_NEW(clp_command_t, clp_new_command, 1, "fetch", NULL, NULL, NULL);
    pull      = T_NEW(clp_command_t, clp_new_command, 2, "pull", NULL, NULL, NULL);
    all_fetch = init_bool_opt("all", "a", false);
    rebase    = init_bool_opt("rebase", "r", false);
    T_ADD(clp_add_command_option, fetch, all_fetch);
    T_ADD(clp_add_command_option, pull, rebase);
    T_ADD(clp_add_command_sub_command, root, fetch);
    T_ADD(clp_add_command_sub_command, root, pull);

    char          *argv[] = {"prog", "fetch", "--all", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == fetch);
    D_TEST_EXPR(all_fetch->value_set == true);
    D_TEST_EXPR(rebase->value_set == false);
    clp_cleanup(root);
}

/* option before 2-level nested subcommand */
static void test_root_option_before_two_level_subcommand(void)
{
    clp_command_t *root, *remote, *add;
    clp_option_t  *verbose, *force;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    remote  = T_NEW(clp_command_t, clp_new_command, 1, "remote", NULL, NULL, NULL);
    add     = T_NEW(clp_command_t, clp_new_command, 2, "add", NULL, NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    force   = init_bool_opt("force", "f", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_option, add, force);
    T_ADD(clp_add_command_sub_command, root, remote);
    T_ADD(clp_add_command_sub_command, remote, add);

    char          *argv[] = {"prog", "-v", "remote", "add", "-f", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(verbose->value_set == true);
    D_TEST_EXPR(force->value_set == true);
    clp_cleanup(root);
}

/* no args to program with subcommands registered — command stays NULL */
static void test_no_args_with_subcommands_registered(void)
{
    clp_command_t *root, *add, *rm;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add  = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    rm   = T_NEW(clp_command_t, clp_new_command, 2, "rm", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, add);
    T_ADD(clp_add_command_sub_command, root, rm);

    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_NULL(cmd);
    clp_cleanup(root);
}

/* subcommand with list operand consumes all trailing args */
static void test_subcommand_with_list_operand(void)
{
    clp_command_t *root, *add;
    clp_operand_t *files;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add   = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    files = init_list_operand("files", false);
    T_ADD(clp_add_command_operand, add, files);
    T_ADD(clp_add_command_sub_command, root, add);

    char          *argv[] = {"prog", "add", "a.c", "b.c", "c.c", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(files->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(files).value_list) == 3);
    clp_cleanup(root);
}

/* ── global options inherited from parent to child ──────────────────────── */

/* global bool option set before subcommand is accessible after dispatch */
static void test_global_option_set_at_parent_accessible_after_dispatch(void)
{
    clp_command_t *root, *push;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push    = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    verbose = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, true);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "--verbose", "push", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(verbose->value_set == true);
    clp_cleanup(root);
}

/* required global option set at parent level satisfies child required check */
static void test_required_global_option_set_at_parent_satisfies_child(void)
{
    clp_command_t *root, *push;
    clp_option_t  *token;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push  = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    token = T_NEW(clp_option_t, t_new_option, "token", "t", NULL, CLP_TYPE_STR, true, true);
    T_ADD(clp_add_command_option, root, token);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "--token", "abc", "push", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(token->value_set == true);
    D_TEST_STR_EQ(TV(token).value_d_string_view.data, "abc");
    clp_cleanup(root);
}

/* required global option NOT set — child required check catches it when child has args */
static void _err_required_global_option_unset_in_child(void)
{
    clp_command_t *root, *push;
    clp_option_t  *token, *force;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push  = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    token = T_NEW(clp_option_t, t_new_option, "token", "t", NULL, CLP_TYPE_STR, true, true);
    force = init_bool_opt("force", "f", false);
    T_ADD(clp_add_command_option, root, token);
    T_ADD(clp_add_command_option, push, force);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "push", "--force", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_required_global_option_unset_exits_via_child(void)
{
    child_result_t r = run_child(_err_required_global_option_unset_in_child);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--token"));
    D_TEST_NOT_NULL(strstr(r.err, "push"));
}

/* non-global required option on parent is NOT inherited — child check ignores it */
static void test_non_global_required_option_not_inherited_to_child(void)
{
    clp_command_t *root, *push;
    clp_option_t  *token;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push  = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    token = T_NEW(clp_option_t, t_new_option, "token", "t", NULL, CLP_TYPE_STR, true, false);
    T_ADD(clp_add_command_option, root, token);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "push", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == push);
    D_TEST_EXPR(token->value_set == false);
    clp_cleanup(root);
}

/* global option propagates all the way to a deeply nested command */
static void test_global_option_inherited_through_nested_levels(void)
{
    clp_command_t *root, *remote, *add;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    remote  = T_NEW(clp_command_t, clp_new_command, 1, "remote", NULL, NULL, NULL);
    add     = T_NEW(clp_command_t, clp_new_command, 2, "add", NULL, NULL, NULL);
    verbose = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, true);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_sub_command, root, remote);
    T_ADD(clp_add_command_sub_command, remote, add);

    char          *argv[] = {"prog", "--verbose", "remote", "add", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(cmd == add);
    D_TEST_EXPR(verbose->value_set == true);
    clp_cleanup(root);
}

/* multiple global options from parent are all inherited */
static void test_multiple_global_options_all_inherited(void)
{
    clp_command_t *root, *push;
    clp_option_t  *verbose, *dry_run;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push    = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    verbose = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, true);
    dry_run = T_NEW(clp_option_t, t_new_option, "dry-run", "n", NULL, CLP_TYPE_BOOL, false, true);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_option, root, dry_run);
    T_ADD(clp_add_command_sub_command, root, push);

    char          *argv[] = {"prog", "--verbose", "--dry-run", "push", NULL};
    clp_command_t *cmd    = NULL;
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
    clp_command_t *root, *cluster, *node, *process, *task, *run;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    cluster = T_NEW(clp_command_t, clp_new_command, 1, "cluster", NULL, NULL, NULL);
    node    = T_NEW(clp_command_t, clp_new_command, 2, "node", NULL, NULL, NULL);
    process = T_NEW(clp_command_t, clp_new_command, 3, "process", NULL, NULL, NULL);
    task    = T_NEW(clp_command_t, clp_new_command, 4, "task", NULL, NULL, NULL);
    run     = T_NEW(clp_command_t, clp_new_command, 5, "run", NULL, NULL, NULL);

    /* one option at each intermediate level */
    clp_option_t *trace, *region, *node_id, *proc_pid, *task_pri;
    trace    = init_bool_opt("trace", "t", false);
    region   = init_str_opt("region", "R", false);
    node_id  = T_NEW(clp_option_t, t_new_option, "node-id", "N", NULL, CLP_TYPE_USIZE, false, false);
    proc_pid = T_NEW(clp_option_t, t_new_option, "pid", "P", NULL, CLP_TYPE_LONG, false, false);
    task_pri = T_NEW(clp_option_t, t_new_option, "priority", "p", NULL, CLP_TYPE_CHAR, false, false);

    T_ADD(clp_add_command_option, root, trace);
    T_ADD(clp_add_command_option, cluster, region);
    T_ADD(clp_add_command_option, node, node_id);
    T_ADD(clp_add_command_option, process, proc_pid);
    T_ADD(clp_add_command_option, task, task_pri);

    /* 20 options on "run": 10 bools + 5 str + 2 usize + 1 long + 1 char + 1 count */
    clp_option_t *verbose, *dry_run, *force, *quiet, *all, *recursive, *no_cache, *strict, *opt_async, *parallel;
    clp_option_t *output, *config, *format, *target, *profile;
    clp_option_t *jobs, *retries, *timeout, *sep, *debug;

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

    jobs    = T_NEW(clp_option_t, t_new_option, "jobs", "j", NULL, CLP_TYPE_USIZE, false, false);
    retries = T_NEW(clp_option_t, t_new_option, "retries", "x", NULL, CLP_TYPE_USIZE, false, false);
    timeout = T_NEW(clp_option_t, t_new_option, "timeout", "m", NULL, CLP_TYPE_LONG, false, false);
    sep     = T_NEW(clp_option_t, t_new_option, "separator", "S", NULL, CLP_TYPE_CHAR, false, false);
    debug   = init_count_opt("debug", "d");

    T_ADD(clp_add_command_option, run, verbose);
    T_ADD(clp_add_command_option, run, dry_run);
    T_ADD(clp_add_command_option, run, force);
    T_ADD(clp_add_command_option, run, quiet);
    T_ADD(clp_add_command_option, run, all);
    T_ADD(clp_add_command_option, run, recursive);
    T_ADD(clp_add_command_option, run, no_cache);
    T_ADD(clp_add_command_option, run, strict);
    T_ADD(clp_add_command_option, run, opt_async);
    T_ADD(clp_add_command_option, run, parallel);
    T_ADD(clp_add_command_option, run, output);
    T_ADD(clp_add_command_option, run, config);
    T_ADD(clp_add_command_option, run, format);
    T_ADD(clp_add_command_option, run, target);
    T_ADD(clp_add_command_option, run, profile);
    T_ADD(clp_add_command_option, run, jobs);
    T_ADD(clp_add_command_option, run, retries);
    T_ADD(clp_add_command_option, run, timeout);
    T_ADD(clp_add_command_option, run, sep);
    T_ADD(clp_add_command_option, run, debug);

    /* 20 operands on "run": 19 SET (op01-op19) + 1 LIST (files) */
    clp_operand_t *op01, *op02, *op03, *op04, *op05, *op06, *op07, *op08, *op09, *op10;
    clp_operand_t *op11, *op12, *op13, *op14, *op15, *op16, *op17, *op18, *op19, *files;

    op01 = init_str_operand("op01", false);
    op02 = init_str_operand("op02", false);
    op03 = init_str_operand("op03", false);
    op04 = init_str_operand("op04", false);
    op05 = init_str_operand("op05", false);
    op06 = init_str_operand("op06", false);
    op07 = init_str_operand("op07", false);
    op08 = init_str_operand("op08", false);
    op09 = init_str_operand("op09", false);
    op10 = init_str_operand("op10", false);
    op11 = init_str_operand("op11", false);
    op12 = init_str_operand("op12", false);
    op13 = init_str_operand("op13", false);
    op14 = init_str_operand("op14", false);
    op15 = init_str_operand("op15", false);
    op16 = init_str_operand("op16", false);
    op17 = init_str_operand("op17", false);
    op18 = init_str_operand("op18", false);
    op19 = init_str_operand("op19", false);
    files = init_list_operand("files", false);

    T_ADD(clp_add_command_operand, run, op01);
    T_ADD(clp_add_command_operand, run, op02);
    T_ADD(clp_add_command_operand, run, op03);
    T_ADD(clp_add_command_operand, run, op04);
    T_ADD(clp_add_command_operand, run, op05);
    T_ADD(clp_add_command_operand, run, op06);
    T_ADD(clp_add_command_operand, run, op07);
    T_ADD(clp_add_command_operand, run, op08);
    T_ADD(clp_add_command_operand, run, op09);
    T_ADD(clp_add_command_operand, run, op10);
    T_ADD(clp_add_command_operand, run, op11);
    T_ADD(clp_add_command_operand, run, op12);
    T_ADD(clp_add_command_operand, run, op13);
    T_ADD(clp_add_command_operand, run, op14);
    T_ADD(clp_add_command_operand, run, op15);
    T_ADD(clp_add_command_operand, run, op16);
    T_ADD(clp_add_command_operand, run, op17);
    T_ADD(clp_add_command_operand, run, op18);
    T_ADD(clp_add_command_operand, run, op19);
    T_ADD(clp_add_command_operand, run, files);

    /* wire hierarchy */
    T_ADD(clp_add_command_sub_command, root, cluster);
    T_ADD(clp_add_command_sub_command, cluster, node);
    T_ADD(clp_add_command_sub_command, node, process);
    T_ADD(clp_add_command_sub_command, process, task);
    T_ADD(clp_add_command_sub_command, task, run);

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

    clp_command_t *cmd = NULL;
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
    D_TEST_EXPR(trace->value_set == true && TV(trace).value_bool == true);
    D_TEST_EXPR(region->value_set == true);
    D_TEST_STR_EQ(TV(region).value_d_string_view.data, "us-east");
    D_TEST_EXPR(node_id->value_set == true && TV(node_id).value_usize == 42);
    D_TEST_EXPR(proc_pid->value_set == true && TV(proc_pid).value_long == -7);
    D_TEST_EXPR(task_pri->value_set == true && TV(task_pri).value_char == 'H');

    /* 10 bools */
    D_TEST_EXPR(verbose->value_set == true && TV(verbose).value_bool == true);
    D_TEST_EXPR(dry_run->value_set == true && TV(dry_run).value_bool == true);
    D_TEST_EXPR(force->value_set == true && TV(force).value_bool == true);
    D_TEST_EXPR(quiet->value_set == true && TV(quiet).value_bool == true);
    D_TEST_EXPR(all->value_set == true && TV(all).value_bool == true);
    D_TEST_EXPR(recursive->value_set == true && TV(recursive).value_bool == true);
    D_TEST_EXPR(no_cache->value_set == true && TV(no_cache).value_bool == true);
    D_TEST_EXPR(strict->value_set == true && TV(strict).value_bool == true);
    D_TEST_EXPR(opt_async->value_set == true && TV(opt_async).value_bool == true);
    D_TEST_EXPR(parallel->value_set == true && TV(parallel).value_bool == true);

    /* 5 strs */
    D_TEST_EXPR(output->value_set == true);
    D_TEST_STR_EQ(TV(output).value_d_string_view.data, "result.txt");
    D_TEST_EXPR(config->value_set == true);
    D_TEST_STR_EQ(TV(config).value_d_string_view.data, "config.yml");
    D_TEST_EXPR(format->value_set == true);
    D_TEST_STR_EQ(TV(format).value_d_string_view.data, "json");
    D_TEST_EXPR(target->value_set == true);
    D_TEST_STR_EQ(TV(target).value_d_string_view.data, "prod");
    D_TEST_EXPR(profile->value_set == true);
    D_TEST_STR_EQ(TV(profile).value_d_string_view.data, "myprofile");

    /* 2 usize + 1 long + 1 char + 1 count */
    D_TEST_EXPR(jobs->value_set == true && TV(jobs).value_usize == 8);
    D_TEST_EXPR(retries->value_set == true && TV(retries).value_usize == 3);
    D_TEST_EXPR(timeout->value_set == true && TV(timeout).value_long == -60);
    D_TEST_EXPR(sep->value_set == true && TV(sep).value_char == ',');
    D_TEST_EXPR(TV(debug).value_usize == 3);

    /* 19 SET operands */
    D_TEST_EXPR(op01->value_set == true);
    D_TEST_STR_EQ(TV(op01).value_d_string_view.data, "val01");
    D_TEST_EXPR(op02->value_set == true);
    D_TEST_STR_EQ(TV(op02).value_d_string_view.data, "val02");
    D_TEST_EXPR(op03->value_set == true);
    D_TEST_STR_EQ(TV(op03).value_d_string_view.data, "val03");
    D_TEST_EXPR(op04->value_set == true);
    D_TEST_STR_EQ(TV(op04).value_d_string_view.data, "val04");
    D_TEST_EXPR(op05->value_set == true);
    D_TEST_STR_EQ(TV(op05).value_d_string_view.data, "val05");
    D_TEST_EXPR(op06->value_set == true);
    D_TEST_STR_EQ(TV(op06).value_d_string_view.data, "val06");
    D_TEST_EXPR(op07->value_set == true);
    D_TEST_STR_EQ(TV(op07).value_d_string_view.data, "val07");
    D_TEST_EXPR(op08->value_set == true);
    D_TEST_STR_EQ(TV(op08).value_d_string_view.data, "val08");
    D_TEST_EXPR(op09->value_set == true);
    D_TEST_STR_EQ(TV(op09).value_d_string_view.data, "val09");
    D_TEST_EXPR(op10->value_set == true);
    D_TEST_STR_EQ(TV(op10).value_d_string_view.data, "val10");
    D_TEST_EXPR(op11->value_set == true);
    D_TEST_STR_EQ(TV(op11).value_d_string_view.data, "val11");
    D_TEST_EXPR(op12->value_set == true);
    D_TEST_STR_EQ(TV(op12).value_d_string_view.data, "val12");
    D_TEST_EXPR(op13->value_set == true);
    D_TEST_STR_EQ(TV(op13).value_d_string_view.data, "val13");
    D_TEST_EXPR(op14->value_set == true);
    D_TEST_STR_EQ(TV(op14).value_d_string_view.data, "val14");
    D_TEST_EXPR(op15->value_set == true);
    D_TEST_STR_EQ(TV(op15).value_d_string_view.data, "val15");
    D_TEST_EXPR(op16->value_set == true);
    D_TEST_STR_EQ(TV(op16).value_d_string_view.data, "val16");
    D_TEST_EXPR(op17->value_set == true);
    D_TEST_STR_EQ(TV(op17).value_d_string_view.data, "val17");
    D_TEST_EXPR(op18->value_set == true);
    D_TEST_STR_EQ(TV(op18).value_d_string_view.data, "val18");
    D_TEST_EXPR(op19->value_set == true);
    D_TEST_STR_EQ(TV(op19).value_d_string_view.data, "val19");

    /* LIST operand: 3 entries */
    D_TEST_EXPR(files->value_set == true);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(files).value_list) == 3);
    d_string_view_t fv;
    D_TEST_EXPR(d_dyn_array_get_elem_at(&TV(files).value_list, 0, &fv) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(fv, "fa.c"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&TV(files).value_list, 1, &fv) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(fv, "fb.c"));
    D_TEST_EXPR(d_dyn_array_get_elem_at(&TV(files).value_list, 2, &fv) == D_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(fv, "fc.c"));

    clp_cleanup(root);
}

/* ── error / exit tests ─────────────────────────────────────────────────── */

/* unknown long option */
static void _err_unknown_long_opt(void)
{
    clp_command_t *root;
    root                  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    char          *argv[] = {"prog", "--unknown", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_unknown_long_option_exits(void)
{
    child_result_t r = run_child(_err_unknown_long_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "--unknown"));
}

/* unknown short option */
static void _err_unknown_short_opt(void)
{
    clp_command_t *root;
    root                  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    char          *argv[] = {"prog", "-z", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_unknown_short_option_exits(void)
{
    child_result_t r = run_child(_err_unknown_short_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "-z"));
}

/* missing required option */
static void _err_missing_required_opt(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = T_NEW(clp_option_t, t_new_option, "output", "o", NULL, CLP_TYPE_STR, true, false);
    T_ADD(clp_add_command_option, root, output);
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_option_exits(void)
{
    child_result_t r = run_child(_err_missing_required_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--output"));
}

/* missing required operand */
static void _err_missing_required_operand(void)
{
    clp_command_t *root;
    clp_operand_t *file;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    file = T_NEW(clp_operand_t, t_new_operand, "file", NULL, CLP_TYPE_STR, true);
    T_ADD(clp_add_command_operand, root, file);
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_operand_exits(void)
{
    child_result_t r = run_child(_err_missing_required_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following operands were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "<file>"));
}

/* too many operands */
static void _err_too_many_operands(void)
{
    clp_command_t *root;
    clp_operand_t *file;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    file = T_NEW(clp_operand_t, t_new_operand, "file", NULL, CLP_TYPE_STR, false);
    T_ADD(clp_add_command_operand, root, file);
    char          *argv[] = {"prog", "a.txt", "b.txt", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_too_many_operands_exits(void)
{
    child_result_t r = run_child(_err_too_many_operands);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "too many operands"));
}

/* invalid value for CLP_TYPE_USIZE */
static void _err_invalid_usize_value(void)
{
    clp_command_t *root;
    clp_option_t  *jobs;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    jobs = T_NEW(clp_option_t, t_new_option, "jobs", "j", NULL, CLP_TYPE_USIZE, false, false);
    T_ADD(clp_add_command_option, root, jobs);
    char          *argv[] = {"prog", "--jobs", "notanumber", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_usize_value_exits(void)
{
    child_result_t r = run_child(_err_invalid_usize_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notanumber"));
    D_TEST_NOT_NULL(strstr(r.err, "--jobs"));
}

/* invalid value for CLP_TYPE_LONG */
static void _err_invalid_long_value(void)
{
    clp_command_t *root;
    clp_option_t  *level;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    level = T_NEW(clp_option_t, t_new_option, "level", "l", NULL, CLP_TYPE_LONG, false, false);
    T_ADD(clp_add_command_option, root, level);
    char          *argv[] = {"prog", "--level", "abc", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_long_value_exits(void)
{
    child_result_t r = run_child(_err_invalid_long_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "abc"));
    D_TEST_NOT_NULL(strstr(r.err, "--level"));
}

/* invalid value for CLP_TYPE_CHAR: more than one character */
static void _err_invalid_char_too_many(void)
{
    clp_command_t *root;
    clp_option_t  *sep;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    sep  = T_NEW(clp_option_t, t_new_option, "sep", "s", NULL, CLP_TYPE_CHAR, false, false);
    T_ADD(clp_add_command_option, root, sep);
    char          *argv[] = {"prog", "--sep", "ab", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_char_value_too_many_chars_exits(void)
{
    child_result_t r = run_child(_err_invalid_char_too_many);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "too many characters"));
    D_TEST_NOT_NULL(strstr(r.err, "--sep"));
}

/* invalid value for CLP_TYPE_CHAR: empty string */
static void _err_invalid_char_empty(void)
{
    clp_command_t *root;
    clp_option_t  *sep;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    sep  = T_NEW(clp_option_t, t_new_option, "sep", "s", NULL, CLP_TYPE_CHAR, false, false);
    T_ADD(clp_add_command_option, root, sep);
    char          *argv[] = {"prog", "--sep", "", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_char_value_empty_string_exits(void)
{
    child_result_t r = run_child(_err_invalid_char_empty);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot parse char"));
    D_TEST_NOT_NULL(strstr(r.err, "--sep"));
}

/* bool option given inline value: --verbose=foo */
static void _err_bool_opt_inline_value(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, false);
    T_ADD(clp_add_command_option, root, verbose);
    char          *argv[] = {"prog", "--verbose=foo", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_bool_opt_with_inline_value_exits(void)
{
    child_result_t r = run_child(_err_bool_opt_inline_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "doesn't allow an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "--verbose"));
}

/* count option given inline value: --debug=3 */
static void _err_count_opt_inline_value(void)
{
    clp_command_t *root;
    clp_option_t  *debug;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    debug = T_NEW(clp_option_t, t_new_option_count, "debug", "d", NULL, false);
    T_ADD(clp_add_command_option, root, debug);
    char          *argv[] = {"prog", "--debug=3", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_count_opt_with_inline_value_exits(void)
{
    child_result_t r = run_child(_err_count_opt_inline_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "doesn't allow an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "--debug"));
}

/* str option with no next argument (last in argv) */
static void _err_str_opt_no_value(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = T_NEW(clp_option_t, t_new_option, "output", "o", NULL, CLP_TYPE_STR, false, false);
    T_ADD(clp_add_command_option, root, output);
    char          *argv[] = {"prog", "--output", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_str_opt_missing_value_exits(void)
{
    child_result_t r = run_child(_err_str_opt_no_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "option require an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "--output"));
}

/* duplicate long option name on same command */
static void _err_duplicate_long_name(void)
{
    clp_command_t *root;
    clp_option_t  *a, *b;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    a    = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, false);
    b    = T_NEW(clp_option_t, t_new_option, "verbose", "w", NULL, CLP_TYPE_BOOL, false, false);
    T_ADD(clp_add_command_option, root, a);
    T_ADD(clp_add_command_option, root, b);
}
static void test_duplicate_long_option_name_exits(void)
{
    child_result_t r = run_child(_err_duplicate_long_name);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "already registered"));
    D_TEST_NOT_NULL(strstr(r.err, "--verbose"));
}

/* duplicate short option name on same command */
static void _err_duplicate_short_name(void)
{
    clp_command_t *root;
    clp_option_t  *a, *b;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    a    = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, false);
    b    = T_NEW(clp_option_t, t_new_option, "vverbose", "v", NULL, CLP_TYPE_BOOL, false, false);
    T_ADD(clp_add_command_option, root, a);
    T_ADD(clp_add_command_option, root, b);
}
static void test_duplicate_short_option_name_exits(void)
{
    child_result_t r = run_child(_err_duplicate_short_name);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "already registered"));
    D_TEST_NOT_NULL(strstr(r.err, "-v"));
}

/* required operand registered after optional one */
static void _err_required_after_optional_operand(void)
{
    clp_command_t *root;
    clp_operand_t *opt_op, *req_op;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    opt_op = T_NEW(clp_operand_t, t_new_operand, "optional", NULL, CLP_TYPE_STR, false);
    req_op = T_NEW(clp_operand_t, t_new_operand, "required", NULL, CLP_TYPE_STR, true);
    T_ADD(clp_add_command_operand, root, opt_op);
    T_ADD(clp_add_command_operand, root, req_op);
}
static void test_required_operand_after_optional_exits(void)
{
    child_result_t r = run_child(_err_required_after_optional_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "cannot follow optional operand"));
    D_TEST_NOT_NULL(strstr(r.err, "required"));
    D_TEST_NOT_NULL(strstr(r.err, "optional"));
}

/* invalid long option name at init: starts with hyphen */
static void _err_long_opt_name_starts_with_hyphen(void)
{
    T_NEW(clp_option_t, t_new_option_raw, "-bad", "b", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_BOOL, false, false, NULL);
}
static void test_long_opt_name_starting_with_hyphen_exits(void)
{
    child_result_t r = run_child(_err_long_opt_name_starts_with_hyphen);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "a long option must"));
}

/* invalid long option name at init: starts with underscore */
static void _err_long_opt_name_starts_with_underscore(void)
{
    T_NEW(clp_option_t, t_new_option_raw, "_bad", "b", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_BOOL, false, false, NULL);
}
static void test_long_opt_name_starting_with_underscore_exits(void)
{
    child_result_t r = run_child(_err_long_opt_name_starts_with_underscore);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "a long option must"));
}

/* invalid short option name at init: non-alphanumeric */
static void _err_short_opt_name_invalid(void)
{
    T_NEW(clp_option_t, t_new_option_raw, "verbose", "!", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_BOOL, false, false, NULL);
}
static void test_short_opt_name_non_alnum_exits(void)
{
    child_result_t r = run_child(_err_short_opt_name_invalid);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "alphanumeric"));
}

/* count action with non-usize type is rejected at init */
static void _err_count_action_with_bool_type(void)
{
    T_NEW(clp_option_t, t_new_option_raw, "verbose", "v", NULL, false, CLP_OPT_ACT_COUNT, (clp_value_t){0}, CLP_TYPE_BOOL, false, false, NULL);
}
static void test_count_action_with_non_usize_type_exits(void)
{
    child_result_t r = run_child(_err_count_action_with_bool_type);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "action 'count'"));
    D_TEST_NOT_NULL(strstr(r.err, "not valid"));
    D_TEST_NOT_NULL(strstr(r.err, "count action requires"));
}

/* count action wrong type via short-only option (different format string branch) */
static void _err_count_action_short_only_wrong_type(void)
{
    T_NEW(clp_option_t, t_new_option_raw, NULL, "v", NULL, false, CLP_OPT_ACT_COUNT, (clp_value_t){0}, CLP_TYPE_BOOL, false, false, NULL);
}
static void test_count_action_short_only_with_non_usize_type_exits(void)
{
    child_result_t r = run_child(_err_count_action_short_only_wrong_type);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "action 'count'"));
    D_TEST_NOT_NULL(strstr(r.err, "not valid"));
    D_TEST_NOT_NULL(strstr(r.err, "count action requires"));
    D_TEST_NOT_NULL(strstr(r.err, "-v"));
}

/* parent-level option is not visible inside subcommand */
static void _err_parent_opt_used_in_subcommand(void)
{
    clp_command_t *root, *add;
    clp_option_t  *force;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    add   = T_NEW(clp_command_t, clp_new_command, 1, "add", NULL, NULL, NULL);
    force = T_NEW(clp_option_t, t_new_option, "force", "f", NULL, CLP_TYPE_BOOL, false, false);
    T_ADD(clp_add_command_option, root, force);
    T_ADD(clp_add_command_sub_command, root, add);
    char          *argv[] = {"prog", "add", "--force", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_parent_option_not_visible_in_subcommand_exits(void)
{
    child_result_t r = run_child(_err_parent_opt_used_in_subcommand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "--force"));
    D_TEST_NOT_NULL(strstr(r.err, "add"));
}

/* missing required option inside a subcommand */
static void _err_missing_required_opt_in_subcommand(void)
{
    clp_command_t *root, *push;
    clp_option_t  *token;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    push  = T_NEW(clp_command_t, clp_new_command, 1, "push", NULL, NULL, NULL);
    token = T_NEW(clp_option_t, t_new_option, "token", "t", NULL, CLP_TYPE_STR, true, false);
    T_ADD(clp_add_command_option, push, token);
    T_ADD(clp_add_command_sub_command, root, push);
    char          *argv[] = {"prog", "push", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_option_in_subcommand_exits(void)
{
    child_result_t r = run_child(_err_missing_required_opt_in_subcommand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--token"));
    D_TEST_NOT_NULL(strstr(r.err, "push"));
}

/* missing required operand inside a subcommand */
static void _err_missing_required_operand_in_subcommand(void)
{
    clp_command_t *root, *cp;
    clp_operand_t *dst;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    cp   = T_NEW(clp_command_t, clp_new_command, 1, "cp", NULL, NULL, NULL);
    dst  = T_NEW(clp_operand_t, t_new_operand, "dst", NULL, CLP_TYPE_STR, true);
    T_ADD(clp_add_command_operand, cp, dst);
    T_ADD(clp_add_command_sub_command, root, cp);
    char          *argv[] = {"prog", "cp", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_missing_required_operand_in_subcommand_exits(void)
{
    child_result_t r = run_child(_err_missing_required_operand_in_subcommand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following operands were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "<dst>"));
    D_TEST_NOT_NULL(strstr(r.err, "cp"));
}

/* duplicate operand name emits a warning but does NOT exit */
static void _warn_duplicate_operand_name(void)
{
    clp_command_t *root;
    clp_operand_t *a, *b;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    a    = T_NEW(clp_operand_t, t_new_operand, "file", NULL, CLP_TYPE_STR, false);
    b    = T_NEW(clp_operand_t, t_new_operand, "file", NULL, CLP_TYPE_STR, false);
    T_ADD(clp_add_command_operand, root, a);
    T_ADD(clp_add_command_operand, root, b);
}
static void test_duplicate_operand_name_warns_no_exit(void)
{
    /* the warning itself is only printed when built with -DWARNING */
    child_result_t r = run_child(_warn_duplicate_operand_name);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
}

/* ── builder status tests (no exit, status + clp_error_t returned) ─────────── */

static void test_builder_duplicate_long_returns_status(void)
{
    clp_error_t    err;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *a    = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, false);
    clp_option_t  *b    = T_NEW(clp_option_t, t_new_option, "verbose", "w", NULL, CLP_TYPE_BOOL, false, false);
    D_TEST_EXPR(clp_add_command_option(root, a, &err) == CLP_OK);
    D_TEST_EXPR(clp_add_command_option(root, b, &err) == CLP_ERR_DUPLICATE);
    D_TEST_EXPR(err.code == CLP_ERR_DUPLICATE);
    D_TEST_EXPR(err.command == root);
    D_TEST_NOT_NULL(strstr(err.message, "--verbose"));
    D_TEST_EXPR(clp_add_command_option(root, b, NULL) == CLP_ERR_DUPLICATE);
    free(b);
    clp_cleanup(root);
}

static void test_builder_invalid_names_return_status(void)
{
    clp_error_t    err;
    clp_option_t  *opt     = (clp_option_t *) 1;
    clp_operand_t *operand = (clp_operand_t *) 1;
    D_TEST_EXPR(t_new_option(&opt, "-bad", NULL, NULL, CLP_TYPE_BOOL, false, false, &err) == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(opt == NULL);
    D_TEST_EXPR(err.code == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(err.command == NULL);
    D_TEST_EXPR(t_new_option(&opt, NULL, "!", NULL, CLP_TYPE_BOOL, false, false, &err) == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(t_new_option(&opt, "help", NULL, NULL, CLP_TYPE_BOOL, false, false, &err) == CLP_ERR_RESERVED_NAME);
    D_TEST_EXPR(t_new_option_raw(&opt, "n", NULL, NULL, false, CLP_OPT_ACT_COUNT, (clp_value_t){0}, CLP_TYPE_STR, false, false, NULL, &err) == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(t_new_operand(&operand, "_x", NULL, CLP_TYPE_STR, false, NULL) == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(operand == NULL);
}

static void test_builder_conflict_and_order_return_status(void)
{
    clp_error_t    err;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_command_t *sub  = T_NEW(clp_command_t, clp_new_command, 1, "sub", NULL, NULL, NULL);
    clp_operand_t *opt  = T_NEW(clp_operand_t, t_new_operand, "optional", NULL, CLP_TYPE_STR, false);
    clp_operand_t *req  = T_NEW(clp_operand_t, t_new_operand, "required", NULL, CLP_TYPE_STR, true);
    D_TEST_EXPR(clp_add_command_operand(root, opt, &err) == CLP_OK);
    D_TEST_EXPR(clp_add_command_operand(root, req, &err) == CLP_ERR_OPERAND_ORDER);
    D_TEST_NOT_NULL(strstr(err.message, "cannot follow optional operand"));
    D_TEST_EXPR(clp_add_command_sub_command(root, sub, &err) == CLP_ERR_CONFLICT);
    D_TEST_EXPR(sub->parent_command == NULL);
    D_TEST_NOT_NULL(strstr(err.message, "cannot have both operands and subcommands"));
    free(req);
    clp_cleanup(sub);
    clp_cleanup(root);
}

static void test_status_to_str_covers_builder_codes(void)
{
    D_TEST_EXPR(strcmp(clp_status_to_str(CLP_ERR_DUPLICATE), "duplicate option") == 0);
    D_TEST_EXPR(strcmp(clp_status_to_str(CLP_ERR_RESERVED_NAME), "reserved name") == 0);
    D_TEST_EXPR(strcmp(clp_status_to_str(CLP_ERR_INVALID_TYPE), "invalid type for action") == 0);
    D_TEST_EXPR(strcmp(clp_status_to_str(CLP_ERR_CONFLICT), "operands and subcommands conflict") == 0);
    D_TEST_EXPR(strcmp(clp_status_to_str(CLP_ERR_OPERAND_ORDER), "required operand after optional operand") == 0);
    D_TEST_EXPR(strcmp(clp_status_to_str(CLP_ERR_VALIDATOR), "rejected by validator") == 0);
}

/* invalid long option name: contains a space (invalid mid-name char) */
static void _err_long_opt_name_invalid_chars(void)
{
    T_NEW(clp_option_t, t_new_option_raw, "bad name", "b", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_BOOL, false, false, NULL);
}
static void test_long_opt_name_with_invalid_chars_exits(void)
{
    child_result_t r = run_child(_err_long_opt_name_invalid_chars);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "is not a valid option name"));
    D_TEST_NOT_NULL(strstr(r.err, "a long option must"));
}

/* short option requires argument but is the last token (short prefix in msg) */
static void _err_short_opt_no_value(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = T_NEW(clp_option_t, t_new_option, "output", "o", NULL, CLP_TYPE_STR, false, false);
    T_ADD(clp_add_command_option, root, output);
    char          *argv[] = {"prog", "-o", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_short_opt_missing_value_exits(void)
{
    child_result_t r = run_child(_err_short_opt_no_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "option require an argument"));
    D_TEST_NOT_NULL(strstr(r.err, "-o"));
}

/* invalid value delivered via short form: -j abc where j is CLP_TYPE_USIZE */
static void _err_invalid_value_via_short_opt(void)
{
    clp_command_t *root;
    clp_option_t  *jobs;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    jobs = T_NEW(clp_option_t, t_new_option, "jobs", "j", NULL, CLP_TYPE_USIZE, false, false);
    T_ADD(clp_add_command_option, root, jobs);
    char          *argv[] = {"prog", "-j", "abc", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_value_via_short_opt_exits(void)
{
    child_result_t r = run_child(_err_invalid_value_via_short_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "abc"));
    D_TEST_NOT_NULL(strstr(r.err, "-j"));
}

/* invalid value for CLP_TYPE_DOUBLE option */
static void _err_invalid_double_opt_value(void)
{
    clp_command_t *root;
    clp_option_t  *rate;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    rate = T_NEW(clp_option_t, t_new_option, "rate", "r", NULL, CLP_TYPE_DOUBLE, false, false);
    T_ADD(clp_add_command_option, root, rate);
    char          *argv[] = {"prog", "--rate", "notadouble", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_double_option_value_exits(void)
{
    child_result_t r = run_child(_err_invalid_double_opt_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notadouble"));
    D_TEST_NOT_NULL(strstr(r.err, "--rate"));
}

/* invalid value for a CLP_TYPE_USIZE operand */
static void _err_invalid_usize_operand(void)
{
    clp_command_t *root;
    clp_operand_t *count;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    count = T_NEW(clp_operand_t, t_new_operand, "count", NULL, CLP_TYPE_USIZE, false);
    T_ADD(clp_add_command_operand, root, count);
    char          *argv[] = {"prog", "notanumber", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_usize_operand_value_exits(void)
{
    child_result_t r = run_child(_err_invalid_usize_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notanumber"));
    D_TEST_NOT_NULL(strstr(r.err, "<count>"));
}

/* invalid value for a CLP_TYPE_LONG operand */
static void _err_invalid_long_operand(void)
{
    clp_command_t *root;
    clp_operand_t *offset;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    offset = T_NEW(clp_operand_t, t_new_operand, "offset", NULL, CLP_TYPE_LONG, false);
    T_ADD(clp_add_command_operand, root, offset);
    char          *argv[] = {"prog", "xyz", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_long_operand_value_exits(void)
{
    child_result_t r = run_child(_err_invalid_long_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "xyz"));
    D_TEST_NOT_NULL(strstr(r.err, "<offset>"));
}

/* invalid value for a CLP_TYPE_CHAR operand (too many chars) */
static void _err_invalid_char_operand(void)
{
    clp_command_t *root;
    clp_operand_t *delim;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    delim = T_NEW(clp_operand_t, t_new_operand, "delim", NULL, CLP_TYPE_CHAR, false);
    T_ADD(clp_add_command_operand, root, delim);
    char          *argv[] = {"prog", "ab", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_char_operand_value_exits(void)
{
    child_result_t r = run_child(_err_invalid_char_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "too many characters"));
    D_TEST_NOT_NULL(strstr(r.err, "<delim>"));
}

/* invalid value for a CLP_TYPE_DOUBLE operand */
static void _err_invalid_double_operand(void)
{
    clp_command_t *root;
    clp_operand_t *scale;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    scale = T_NEW(clp_operand_t, t_new_operand, "scale", NULL, CLP_TYPE_DOUBLE, false);
    T_ADD(clp_add_command_operand, root, scale);
    char          *argv[] = {"prog", "notdouble", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_invalid_double_operand_value_exits(void)
{
    child_result_t r = run_child(_err_invalid_double_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "invalid value"));
    D_TEST_NOT_NULL(strstr(r.err, "notdouble"));
    D_TEST_NOT_NULL(strstr(r.err, "<scale>"));
}

/* ── input checkers ─────────────────────────────────────────────────────── */

static int checker_calls;

static const char *check_positive_long(const void *p)
{
    checker_calls++;
    return *(const long *) p > 0 ? NULL : "must be positive";
}

static const char *check_port_range(const void *p)
{
    checker_calls++;
    usize port = *(const usize *) p;
    return (port >= 1 && port <= 65535) ? NULL : "port out of range [1 65535]";
}

static const char *check_no_dot_str(const void *p)
{
    checker_calls++;
    d_string_view_t s = *(const d_string_view_t *) p;
    return memchr(s.data, '.', s.size) == NULL ? NULL : "must not contain '.'";
}

/* option checker accepts a valid value: parse succeeds and value is stored */
static void test_option_checker_accepts_valid_value(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *num  = T_NEW(clp_option_t, t_new_option_raw, "num", "n", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_LONG, false, false, check_positive_long);
    T_ADD(clp_add_command_option, root, num);
    char          *argv[] = {"prog", "--num", "42", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(checker_calls == 1);
    D_TEST_EXPR(num->value_set == true);
    D_TEST_EXPR(TV(num).value_long == 42);
    clp_cleanup(root);
}

/* option checker rejects: CLP_ERR_BAD_VALUE with the checker's message */
static void test_option_checker_rejects_value(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *num  = T_NEW(clp_option_t, t_new_option_raw, "num", "n", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_LONG, false, false, check_positive_long);
    T_ADD(clp_add_command_option, root, num);
    char          *argv[] = {"prog", "--num", "-3", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(e.code == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(checker_calls == 1);
    D_TEST_NOT_NULL(strstr(e.message, "invalid value"));
    D_TEST_NOT_NULL(strstr(e.message, "-3"));
    D_TEST_NOT_NULL(strstr(e.message, "--num"));
    D_TEST_NOT_NULL(strstr(e.message, "must be positive"));
    clp_cleanup(root);
}

/* option checker rejects a value given through the short form */
static void test_option_checker_rejects_short_form(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *port = T_NEW(clp_option_t, t_new_option_raw, "port", "p", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_USIZE, false, false, check_port_range);
    T_ADD(clp_add_command_option, root, port);
    char          *argv[] = {"prog", "-p", "70000", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "-p"));
    D_TEST_NOT_NULL(strstr(e.message, "70000"));
    D_TEST_NOT_NULL(strstr(e.message, "port out of range"));
    clp_cleanup(root);
}

/* checker sees the parsed string value */
static void test_option_checker_on_string_value(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *name = T_NEW(clp_option_t, t_new_option_raw, "name", NULL, NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_STR, false, false, check_no_dot_str);
    T_ADD(clp_add_command_option, root, name);
    char          *ok_argv[]  = {"prog", "--name", "alice", NULL};
    char          *bad_argv[] = {"prog", "--name", "a.b", NULL};
    clp_command_t *cmd        = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, ok_argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(TV(name).value_d_string_view.size == 5);
    D_TEST_EXPR(memcmp(TV(name).value_d_string_view.data, "alice", 5) == 0);
    clp_cleanup(root);

    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    name = T_NEW(clp_option_t, t_new_option_raw, "name", NULL, NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_STR, false, false, check_no_dot_str);
    T_ADD(clp_add_command_option, root, name);
    cmd = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, bad_argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "must not contain '.'"));
    clp_cleanup(root);
}

/* checker is not called when the type conversion already failed */
static void test_option_checker_skipped_on_conversion_error(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *num  = T_NEW(clp_option_t, t_new_option_raw, "num", "n", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_LONG, false, false, check_positive_long);
    T_ADD(clp_add_command_option, root, num);
    char          *argv[] = {"prog", "--num", "abc", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(checker_calls == 0);
    D_TEST_EXPR(strstr(e.message, "must be positive") == NULL);
    clp_cleanup(root);
}

/* checker is not called when the option is absent; default value is kept */
static void test_option_checker_not_called_when_absent(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *num  = T_NEW(clp_option_t, t_new_option_raw, "num", "n", NULL, true, CLP_OPT_ACT_SET, (clp_value_t){.value_long = -1}, CLP_TYPE_LONG, false, false, check_positive_long);
    T_ADD(clp_add_command_option, root, num);
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(checker_calls == 0);
    D_TEST_EXPR(TV(num).value_long == -1);
    clp_cleanup(root);
}

/* convenience macros leave the checker unset */
static void test_option_macro_has_no_checker(void)
{
    clp_option_t  *opt     = init_str_opt("name", "n", false);
    clp_operand_t *operand = init_str_operand("file", false);
    D_TEST_EXPR(opt->checker == NULL);
    D_TEST_EXPR(operand->checker == NULL);
    clp_free_option(opt);
    clp_free_operand(operand);
}

/* operand checker accepts a valid value */
static void test_operand_checker_accepts_valid_value(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_operand_t *port = T_NEW(clp_operand_t, t_new_operand_raw, "port", NULL, false, CLP_OPERAND_ACT_SET, (clp_value_t){0}, CLP_TYPE_USIZE, true, check_port_range);
    T_ADD(clp_add_command_operand, root, port);
    char          *argv[] = {"prog", "8080", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(checker_calls == 1);
    D_TEST_EXPR(port->value_set == true);
    D_TEST_EXPR(TV(port).value_usize == 8080);
    clp_cleanup(root);
}

/* operand checker rejects: error names the operand and carries the message */
static void test_operand_checker_rejects_value(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_operand_t *port = T_NEW(clp_operand_t, t_new_operand_raw, "port", NULL, false, CLP_OPERAND_ACT_SET, (clp_value_t){0}, CLP_TYPE_USIZE, true, check_port_range);
    T_ADD(clp_add_command_operand, root, port);
    char          *argv[] = {"prog", "0", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "<port>"));
    D_TEST_NOT_NULL(strstr(e.message, "port out of range"));
    clp_cleanup(root);
}

/* clp_parse_args exits with failure when a checker rejects */
static void _err_option_checker_rejects(void)
{
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *num  = T_NEW(clp_option_t, t_new_option_raw, "num", "n", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_LONG, false, false, check_positive_long);
    T_ADD(clp_add_command_option, root, num);
    char          *argv[] = {"prog", "--num", "0", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_option_checker_reject_exits(void)
{
    child_result_t r = run_child(_err_option_checker_rejects);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "must be positive"));
    D_TEST_NOT_NULL(strstr(r.err, "--num"));
}

/* _checked macros: checker is stored and runs on parse */
static void test_option_checked_macro_rejects_value(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *num  = T_NEW(clp_option_t, t_new_option_checked, "num", "n", NULL, CLP_TYPE_LONG, false, false, check_positive_long);
    T_ADD(clp_add_command_option, root, num);
    D_TEST_EXPR(num->checker == check_positive_long);
    char          *argv[] = {"prog", "-n", "-1", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "must be positive"));
    clp_cleanup(root);
}

/* _default_*_checked option: default kept when absent, override is checked */
static void test_option_default_checked_macro(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *port = T_NEW(clp_option_t, t_new_option_default_usize_checked, "port", "p", NULL, 8080, false, false, check_port_range);
    T_ADD(clp_add_command_option, root, port);
    D_TEST_EXPR(port->checker == check_port_range);
    char          *absent[] = {"prog", NULL};
    clp_command_t *cmd      = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, absent, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(TV(port).value_usize == 8080);
    clp_cleanup(root);

    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    port = T_NEW(clp_option_t, t_new_option_default_usize_checked, "port", "p", NULL, 8080, false, false, check_port_range);
    T_ADD(clp_add_command_option, root, port);
    char *bad[] = {"prog", "--port", "0", NULL};
    cmd         = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, bad, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "port out of range"));
    D_TEST_EXPR(TV(port).value_usize == 8080); /* rejected value never reaches dest */
    clp_cleanup(root);
}

/* _default_str_checked option keeps the string default */
static void test_option_default_str_checked_macro(void)
{
    clp_option_t *name = T_NEW(clp_option_t, t_new_option_default_str_checked, "name", NULL, NULL, "bob", false, false, check_no_dot_str);
    D_TEST_EXPR(name->checker == check_no_dot_str);
    D_TEST_EXPR(TV(name).value_d_string_view.size == 3);
    D_TEST_EXPR(memcmp(TV(name).value_d_string_view.data, "bob", 3) == 0);
    clp_free_option(name);
}

/* operand _checked macros */
static void test_operand_checked_macros(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_operand_t *port = T_NEW(clp_operand_t, t_new_operand_checked, "port", NULL, CLP_TYPE_USIZE, true, check_port_range);
    T_ADD(clp_add_command_operand, root, port);
    D_TEST_EXPR(port->checker == check_port_range);
    char          *bad[] = {"prog", "99999", NULL};
    clp_command_t *cmd   = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, bad, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "<port>"));
    clp_cleanup(root);

    clp_operand_t *lvl = T_NEW(clp_operand_t, t_new_operand_default_long_checked, "level", NULL, 3, false, check_positive_long);
    D_TEST_EXPR(lvl->checker == check_positive_long);
    D_TEST_EXPR(TV(lvl).value_long == 3);
    clp_free_operand(lvl);
}

/* list option: every comma-separated item and every repetition is checked */
static void test_list_option_checker_checks_each_item(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *tags = T_NEW(clp_option_t, t_new_option_list_checked, "tag", "t", NULL, false, false, check_no_dot_str);
    T_ADD(clp_add_command_option, root, tags);
    char          *ok[] = {"prog", "--tag", "a,b", "-t", "c", NULL};
    clp_command_t *cmd  = NULL;
    checker_calls       = 0;
    D_TEST_EXPR(clp_try_parse_args(root, ok, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(checker_calls == 3);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(tags).value_list) == 3);
    clp_cleanup(root);

    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    tags = T_NEW(clp_option_t, t_new_option_list_checked, "tag", "t", NULL, false, false, check_no_dot_str);
    T_ADD(clp_add_command_option, root, tags);
    char *bad[]   = {"prog", "--tag", "good,x.y,never", NULL};
    cmd           = NULL;
    checker_calls = 0;
    D_TEST_EXPR(clp_try_parse_args(root, bad, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(checker_calls == 2);
    D_TEST_NOT_NULL(strstr(e.message, "'x.y'"));
    D_TEST_NOT_NULL(strstr(e.message, "--tag"));
    D_TEST_NOT_NULL(strstr(e.message, "must not contain '.'"));
    D_TEST_EXPR(strstr(e.message, "never") == NULL);
    clp_cleanup(root);
}

/* list operand: each item is checked */
static void test_list_operand_checker_checks_each_item(void)
{
    clp_error_t    e;
    clp_command_t *root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_operand_t *files = T_NEW(clp_operand_t, t_new_operand_list_checked, "files", NULL, true, check_no_dot_str);
    T_ADD(clp_add_command_operand, root, files);
    char          *ok[] = {"prog", "a", "b", NULL};
    clp_command_t *cmd  = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, ok, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(d_dyn_array_get_size(&TV(files).value_list) == 2);
    clp_cleanup(root);

    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    files = T_NEW(clp_operand_t, t_new_operand_list_checked, "files", NULL, true, check_no_dot_str);
    T_ADD(clp_add_command_operand, root, files);
    char *bad[] = {"prog", "a", "b.c", NULL};
    cmd         = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, bad, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "'b.c'"));
    D_TEST_NOT_NULL(strstr(e.message, "<files>"));
    clp_cleanup(root);
}

/* kv checker: key must be upper-case, value must not contain '.' */
static const char *check_env_pair(const void *p)
{
    checker_calls++;
    const clp_key_value_t *kv = p;
    for (usize i = 0; i < kv->key.size; i++)
        if (kv->key.data[i] < 'A' || kv->key.data[i] > 'Z')
            return "key must be upper-case";
    if (memchr(kv->value.data, '.', kv->value.size) != NULL)
        return "value must not contain '.'";
    return NULL;
}

/* kv option: checker sees each pair split into key and value */
static void test_kv_option_checker_accepts_pairs(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *env  = T_NEW(clp_option_t, t_new_option_kv_checked, "env", "e", NULL, false, false, check_env_pair);
    T_ADD(clp_add_command_option, root, env);
    D_TEST_EXPR(env->checker == check_env_pair);
    char          *argv[] = {"prog", "--env", "HOME=x,USER=bob", "-e", "PATH=bin", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(checker_calls == 3);
    D_TEST_EXPR(d_unordered_map_get_size(&TV(env).value_kv) == 3);
    clp_cleanup(root);
}

/* kv option: bad key rejected, error shows the whole pair; later pairs skipped */
static void test_kv_option_checker_rejects_key(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *env  = T_NEW(clp_option_t, t_new_option_kv_checked, "env", "e", NULL, false, false, check_env_pair);
    T_ADD(clp_add_command_option, root, env);
    char          *argv[] = {"prog", "--env", "HOME=x,user=bob,NEVER=y", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(checker_calls == 2);
    D_TEST_NOT_NULL(strstr(e.message, "'user=bob'"));
    D_TEST_NOT_NULL(strstr(e.message, "--env"));
    D_TEST_NOT_NULL(strstr(e.message, "key must be upper-case"));
    D_TEST_EXPR(strstr(e.message, "NEVER") == NULL);
    clp_cleanup(root);
}

/* kv option: bad value rejected */
static void test_kv_option_checker_rejects_value(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *env  = T_NEW(clp_option_t, t_new_option_kv_checked, "env", "e", NULL, false, false, check_env_pair);
    T_ADD(clp_add_command_option, root, env);
    char          *argv[] = {"prog", "-e", "HOST=a.b", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "'HOST=a.b'"));
    D_TEST_NOT_NULL(strstr(e.message, "value must not contain '.'"));
    clp_cleanup(root);
}

/* kv: malformed pairs are still reported as CLP_ERR_BAD_KV, checker not called */
static void test_kv_checker_not_called_on_malformed_pair(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *env  = T_NEW(clp_option_t, t_new_option_kv_checked, "env", "e", NULL, false, false, check_env_pair);
    T_ADD(clp_add_command_option, root, env);
    char          *argv[] = {"prog", "--env", "NOEQUALS", NULL};
    clp_command_t *cmd    = NULL;
    checker_calls         = 0;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_KV);
    D_TEST_EXPR(checker_calls == 0);
    clp_cleanup(root);
}

/* kv operand: checker runs and error names the operand */
static void test_kv_operand_checker(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_operand_t *vars = T_NEW(clp_operand_t, t_new_operand_kv_checked, "vars", NULL, true, check_env_pair);
    T_ADD(clp_add_command_operand, root, vars);
    char          *ok[] = {"prog", "A=1,B=2", NULL};
    clp_command_t *cmd  = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, ok, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(d_unordered_map_get_size(&TV(vars).value_kv) == 2);
    clp_cleanup(root);

    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    vars = T_NEW(clp_operand_t, t_new_operand_kv_checked, "vars", NULL, true, check_env_pair);
    T_ADD(clp_add_command_operand, root, vars);
    char *bad[] = {"prog", "a=1", NULL};
    cmd         = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, bad, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "'a=1'"));
    D_TEST_NOT_NULL(strstr(e.message, "'vars'"));
    clp_cleanup(root);
}

/* multiple missing required options: all listed before exit */
static void _err_multiple_missing_required_opts(void)
{
    clp_command_t *root;
    clp_option_t  *output, *config;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = T_NEW(clp_option_t, t_new_option, "output", "o", NULL, CLP_TYPE_STR, true, false);
    config = T_NEW(clp_option_t, t_new_option, "config", "c", NULL, CLP_TYPE_STR, true, false);
    T_ADD(clp_add_command_option, root, output);
    T_ADD(clp_add_command_option, root, config);
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_multiple_missing_required_options_exits(void)
{
    child_result_t r = run_child(_err_multiple_missing_required_opts);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following options were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "--output"));
    D_TEST_NOT_NULL(strstr(r.err, "--config"));
}

/* multiple missing required operands: all listed before exit */
static void _err_multiple_missing_required_operands(void)
{
    clp_command_t *root;
    clp_operand_t *src, *dst;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    src  = T_NEW(clp_operand_t, t_new_operand, "src", NULL, CLP_TYPE_STR, true);
    dst  = T_NEW(clp_operand_t, t_new_operand, "dst", NULL, CLP_TYPE_STR, true);
    T_ADD(clp_add_command_operand, root, src);
    T_ADD(clp_add_command_operand, root, dst);
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_multiple_missing_required_operands_exits(void)
{
    child_result_t r = run_child(_err_multiple_missing_required_operands);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "the following operands were not provided"));
    D_TEST_NOT_NULL(strstr(r.err, "<src>"));
    D_TEST_NOT_NULL(strstr(r.err, "<dst>"));
}

/* unknown option in combined short token */
static void _err_unknown_short_in_combined(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    verbose = T_NEW(clp_option_t, t_new_option, "verbose", "v", NULL, CLP_TYPE_BOOL, false, false);
    T_ADD(clp_add_command_option, root, verbose);
    char          *argv[] = {"prog", "-vz", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_unknown_short_in_combined_token_exits(void)
{
    child_result_t r = run_child(_err_unknown_short_in_combined);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "unknown option"));
    D_TEST_NOT_NULL(strstr(r.err, "-z"));
}

/* ── print_usage tests ──────────────────────────────────────────────────── */

/* usage line is printed to stderr when required option is missing (no args) */
static void _usage_missing_required_opt(void)
{
    clp_command_t *root;
    clp_option_t  *output;
    root   = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    output = T_NEW(clp_option_t, t_new_option, "output", "o", NULL, CLP_TYPE_STR, true, false);
    T_ADD(clp_add_command_option, root, output);
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_in_stderr_on_missing_required_option(void)
{
    child_result_t r = run_child(_usage_missing_required_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "Usage:"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
}

/* "For more information, try '--help'." is printed alongside the usage line */
static void test_for_more_info_hint_on_missing_required_option(void)
{
    child_result_t r = run_child(_usage_missing_required_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "For more information, try '--help'."));
}

/* usage line is printed to stderr when required operand is missing (no args) */
static void _usage_missing_required_operand(void)
{
    clp_command_t *root;
    clp_operand_t *file;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    file = T_NEW(clp_operand_t, t_new_operand, "file", NULL, CLP_TYPE_STR, true);
    T_ADD(clp_add_command_operand, root, file);
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_in_stderr_on_missing_required_operand(void)
{
    child_result_t r = run_child(_usage_missing_required_operand);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "Usage:"));
    D_TEST_NOT_NULL(strstr(r.err, "prog"));
    D_TEST_NOT_NULL(strstr(r.err, "For more information, try '--help'."));
}

/* usage line in --help output contains "Usage:" and the command name */
static void _usage_in_help_output(void)
{
    clp_command_t *root;
    root                  = T_NEW(clp_command_t, clp_new_command, 0, "mytool", "a test tool", NULL, NULL);
    char          *argv[] = {"mytool", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_appears_in_help_stdout(void)
{
    child_result_t r = run_child_stdout(_usage_in_help_output);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Usage:"));
    D_TEST_NOT_NULL(strstr(r.out, "mytool"));
}

/* usage line in --help shows "<COMMAND>" placeholder for commands with subcommands */
static void _usage_help_with_subcommand(void)
{
    clp_command_t *root, *sub;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", "test", NULL, NULL);
    sub  = T_NEW(clp_command_t, clp_new_command, 1, "sub", "a subcommand", NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, sub);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_shows_command_placeholder(void)
{
    child_result_t r = run_child_stdout(_usage_help_with_subcommand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "<COMMAND>"));
}

/* usage line in --help shows operand names for commands with operands */
static void _usage_help_with_operand(void)
{
    clp_command_t *root;
    clp_operand_t *src, *dst;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", "test", NULL, NULL);
    src  = init_str_operand("source", false);
    dst  = init_str_operand("dest", false);
    T_ADD(clp_add_command_operand, root, src);
    T_ADD(clp_add_command_operand, root, dst);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_shows_operand_names(void)
{
    child_result_t r = run_child_stdout(_usage_help_with_operand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "<source>"));
    D_TEST_NOT_NULL(strstr(r.out, "<dest>"));
}

/* usage line shows "..." ellipsis for list operands */
static void _usage_help_with_list_operand(void)
{
    clp_command_t *root;
    clp_operand_t *files;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", "test", NULL, NULL);
    files = init_list_operand("files", true);
    T_ADD(clp_add_command_operand, root, files);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_shows_ellipsis_for_list_operand(void)
{
    child_result_t r = run_child_stdout(_usage_help_with_list_operand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "<files ...>"));
}

/* usage line in --help includes option names */
static void _usage_help_with_options(void)
{
    clp_command_t *root;
    clp_option_t  *verbose, *output;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", "test", NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    output  = init_str_opt("output", "o", false);
    T_ADD(clp_add_command_option, root, verbose);
    T_ADD(clp_add_command_option, root, output);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_usage_line_includes_option_names(void)
{
    child_result_t r = run_child_stdout(_usage_help_with_options);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "--verbose"));
    D_TEST_NOT_NULL(strstr(r.out, "--output"));
}

/* ── help / -h built-in ─────────────────────────────────────────────────── */

/* registering "help" as a long option name is rejected (reserved) */
static void _err_register_help_as_long_opt(void)
{
    T_NEW(clp_option_t, t_new_option_raw, "help", "H", NULL, false, CLP_OPT_ACT_SET, (clp_value_t){0}, CLP_TYPE_BOOL, false, false, NULL);
}
static void test_registering_help_long_opt_exits(void)
{
    child_result_t r = run_child(_err_register_help_as_long_opt);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "reserved option"));
    D_TEST_NOT_NULL(strstr(r.err, "help"));
}

/* --help exits with success and prints description */
static void _help_long_opt_prog(void)
{
    clp_command_t *root;
    root                  = T_NEW(clp_command_t, clp_new_command, 0, "prog", "a test program", NULL, NULL);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_long_help_exits_success_and_prints_description(void)
{
    child_result_t r = run_child_stdout(_help_long_opt_prog);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Description:"));
    D_TEST_NOT_NULL(strstr(r.out, "a test program"));
}

/* -h exits with success and prints description when no user -h is registered */
static void _help_short_opt_prog(void)
{
    clp_command_t *root;
    root                  = T_NEW(clp_command_t, clp_new_command, 0, "prog", "a test program", NULL, NULL);
    char          *argv[] = {"prog", "-h", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_short_help_exits_success_when_not_user_defined(void)
{
    child_result_t r = run_child_stdout(_help_short_opt_prog);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Description:"));
}

/* -h processes as normal option when user has registered their own -h */
static void test_short_help_not_triggered_when_user_defined_h(void)
{
    clp_command_t *root;
    clp_option_t  *h;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", "test", NULL, NULL);
    h    = T_NEW(clp_option_t, t_new_option, NULL, "h", NULL, CLP_TYPE_BOOL, false, false);
    T_ADD(clp_add_command_option, root, h);
    char          *argv[] = {"prog", "-h", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
    D_TEST_EXPR(h->value_set == true);
    D_TEST_EXPR(TV(h).value_bool == true);
    clp_cleanup(root);
}

/* help output includes Options: section with registered option names */
static void _help_with_options(void)
{
    clp_command_t *root;
    clp_option_t  *verbose;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", "a test program", NULL, NULL);
    verbose = init_bool_opt("verbose", "v", false);
    T_ADD(clp_add_command_option, root, verbose);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_output_includes_options_section(void)
{
    child_result_t r = run_child_stdout(_help_with_options);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Options:"));
    D_TEST_NOT_NULL(strstr(r.out, "--verbose"));
}

/* help output includes Commands: section with registered subcommand names */
static void _help_with_subcommand(void)
{
    clp_command_t *root, *add;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", "a test program", NULL, NULL);
    add  = T_NEW(clp_command_t, clp_new_command, 1, "add", "add files", NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, add);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_output_includes_commands_section(void)
{
    child_result_t r = run_child_stdout(_help_with_subcommand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Commands:"));
    D_TEST_NOT_NULL(strstr(r.out, "add"));
}

/* help output includes Arguments: section with registered operand names */
static void _help_with_operand(void)
{
    clp_command_t *root;
    clp_operand_t *file;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", "a test program", NULL, NULL);
    file = init_str_operand("file", false);
    T_ADD(clp_add_command_operand, root, file);
    char          *argv[] = {"prog", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_output_includes_arguments_section(void)
{
    child_result_t r = run_child_stdout(_help_with_operand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "Arguments:"));
    D_TEST_NOT_NULL(strstr(r.out, "<file>"));
}

/* prog subcmd --help shows the subcommand's own help, not root's */
static void _help_on_subcommand(void)
{
    clp_command_t *root, *push;
    clp_option_t  *force;
    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", "root program", NULL, NULL);
    push  = T_NEW(clp_command_t, clp_new_command, 1, "push", "push changes", NULL, NULL);
    force = init_bool_opt("force", "f", false);
    T_ADD(clp_add_command_option, push, force);
    T_ADD(clp_add_command_sub_command, root, push);
    char          *argv[] = {"prog", "push", "--help", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_help_on_subcommand_shows_subcommand_info(void)
{
    child_result_t r = run_child_stdout(_help_on_subcommand);
    D_TEST_EXPR(r.status == EXIT_SUCCESS);
    D_TEST_NOT_NULL(strstr(r.out, "push changes"));
    D_TEST_NOT_NULL(strstr(r.out, "--force"));
}

/* ── key=value option tests ─────────────────────────────────────────────── */

static void test_kv_option_single_pair(void)
{
    clp_command_t *root;
    clp_option_t  *env;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    env  = init_kv_opt("env", "e");
    T_ADD(clp_add_command_option, root, env);

    char          *argv[] = {"prog", "--env", "HOST=localhost", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);

    D_TEST_EXPR(env->value_set == true);
    d_string_view_t  key = D_STRING_VIEW_FROM_LITERAL("HOST");
    d_string_view_t *val = d_unordered_map_get(&TV(env).value_kv, &key);
    D_TEST_NOT_NULL(val);
    D_TEST_EXPR(d_string_view_compare_against_c_string(*val, "localhost"));
    clp_cleanup(root);
}

static void test_kv_option_multiple_pairs(void)
{
    clp_command_t *root;
    clp_option_t  *env;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    env  = init_kv_opt("env", "e");
    T_ADD(clp_add_command_option, root, env);

    char          *argv[] = {"prog", "--env", "HOST=localhost,PORT=8080", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);

    D_TEST_EXPR(env->value_set == true);
    d_string_view_t  k1 = D_STRING_VIEW_FROM_LITERAL("HOST");
    d_string_view_t  k2 = D_STRING_VIEW_FROM_LITERAL("PORT");
    d_string_view_t *v1 = d_unordered_map_get(&TV(env).value_kv, &k1);
    d_string_view_t *v2 = d_unordered_map_get(&TV(env).value_kv, &k2);
    D_TEST_NOT_NULL(v1);
    D_TEST_NOT_NULL(v2);
    D_TEST_EXPR(d_string_view_compare_against_c_string(*v1, "localhost"));
    D_TEST_EXPR(d_string_view_compare_against_c_string(*v2, "8080"));
    clp_cleanup(root);
}

static void test_kv_operand_single_pair(void)
{
    clp_command_t *root;
    clp_operand_t *binding;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    binding = init_kv_operand("binding", true);
    T_ADD(clp_add_command_operand, root, binding);

    char          *argv[] = {"prog", "HOST=localhost", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);

    D_TEST_EXPR(binding->value_set == true);
    d_string_view_t  key = D_STRING_VIEW_FROM_LITERAL("HOST");
    d_string_view_t *val = d_unordered_map_get(&TV(binding).value_kv, &key);
    D_TEST_NOT_NULL(val);
    D_TEST_EXPR(d_string_view_compare_against_c_string(*val, "localhost"));
    clp_cleanup(root);
}

static void _err_kv_opt_missing_eq(void)
{
    clp_command_t *root;
    clp_option_t  *env;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    env  = init_kv_opt("env", "e");
    T_ADD(clp_add_command_option, root, env);
    char          *argv[] = {"prog", "--env", "HOSTlocalhost", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opt_missing_eq_exits(void)
{
    child_result_t r = run_child(_err_kv_opt_missing_eq);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "missing '='"));
    D_TEST_NOT_NULL(strstr(r.err, "HOSTlocalhost"));
    D_TEST_NOT_NULL(strstr(r.err, "--env"));
}

static void _err_kv_opt_empty_key(void)
{
    clp_command_t *root;
    clp_option_t  *env;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    env  = init_kv_opt("env", "e");
    T_ADD(clp_add_command_option, root, env);
    char          *argv[] = {"prog", "--env", "=localhost", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opt_empty_key_exits(void)
{
    child_result_t r = run_child(_err_kv_opt_empty_key);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty key"));
    D_TEST_NOT_NULL(strstr(r.err, "--env"));
}

static void _err_kv_opt_empty_value(void)
{
    clp_command_t *root;
    clp_option_t  *env;
    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    env  = init_kv_opt("env", "e");
    T_ADD(clp_add_command_option, root, env);
    char          *argv[] = {"prog", "--env", "HOST=", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_opt_empty_value_exits(void)
{
    child_result_t r = run_child(_err_kv_opt_empty_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty value"));
    D_TEST_NOT_NULL(strstr(r.err, "HOST"));
    D_TEST_NOT_NULL(strstr(r.err, "--env"));
}

static void _err_kv_operand_missing_eq(void)
{
    clp_command_t *root;
    clp_operand_t *binding;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    binding = init_kv_operand("binding", true);
    T_ADD(clp_add_command_operand, root, binding);
    char          *argv[] = {"prog", "HOSTlocalhost", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_operand_missing_eq_exits(void)
{
    child_result_t r = run_child(_err_kv_operand_missing_eq);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "missing '='"));
    D_TEST_NOT_NULL(strstr(r.err, "HOSTlocalhost"));
}

static void _err_kv_operand_empty_key(void)
{
    clp_command_t *root;
    clp_operand_t *binding;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    binding = init_kv_operand("binding", true);
    T_ADD(clp_add_command_operand, root, binding);
    char          *argv[] = {"prog", "=localhost", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_operand_empty_key_exits(void)
{
    child_result_t r = run_child(_err_kv_operand_empty_key);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty key"));
}

static void _err_kv_operand_empty_value(void)
{
    clp_command_t *root;
    clp_operand_t *binding;
    root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    binding = init_kv_operand("binding", true);
    T_ADD(clp_add_command_operand, root, binding);
    char          *argv[] = {"prog", "HOST=", NULL};
    clp_command_t *cmd    = NULL;
    clp_parse_args(root, argv, &cmd);
}
static void test_kv_operand_empty_value_exits(void)
{
    child_result_t r = run_child(_err_kv_operand_empty_value);
    D_TEST_EXPR(r.status == EXIT_FAILURE);
    D_TEST_NOT_NULL(strstr(r.err, "empty value"));
    D_TEST_NOT_NULL(strstr(r.err, "HOST"));
}

/* ── main ───────────────────────────────────────────────────────────────── */

/* ── clp_insert_opts (create + register a table of options) ─────────────── */

/* every entry is created, registered in order and bound to its dest */
static void test_insert_opts_creates_and_registers(void)
{
    clp_error_t    e;
    bool           verbose = false;
    long           jobs    = 4;
    usize          port    = 8080;
    clp_command_t *root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);

    clp_option_info_t opts[] = {
        {"verbose", "v", "verbose output", &verbose, CLP_OPT_ACT_SET, CLP_TYPE_BOOL, false, false, NULL, NULL},
        {"jobs", "j", "worker count", &jobs, CLP_OPT_ACT_SET, CLP_TYPE_LONG, false, true, NULL, NULL},
        {"port", NULL, NULL, &port, CLP_OPT_ACT_SET, CLP_TYPE_USIZE, false, false, check_port_range, NULL},
    };
    D_TEST_EXPR(clp_insert_opts(root, opts, sizeof(opts) / sizeof(*opts), &e) == CLP_OK);
    D_TEST_EXPR(d_dyn_array_get_size(&root->options) == 3);

    clp_option_t *o_jobs = clp_get_option_by_long(root, D_STRING_VIEW_FROM_LITERAL("jobs"));
    D_TEST_NOT_NULL(o_jobs);
    D_TEST_EXPR(o_jobs->dest == &jobs && o_jobs->global == true && o_jobs->short_name == 'j');
    clp_option_t *o_port = clp_get_option_by_long(root, D_STRING_VIEW_FROM_LITERAL("port"));
    D_TEST_NOT_NULL(o_port);
    D_TEST_EXPR(o_port->checker == check_port_range);

    char          *argv[] = {"prog", "-v", "--jobs", "12", "--port", "443", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(verbose == true && jobs == 12 && port == 443);
    clp_cleanup(root);
}

/* count 0 is a no-op, infos may then be NULL */
static void test_insert_opts_empty(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    D_TEST_EXPR(clp_insert_opts(root, NULL, 0, &e) == CLP_OK);
    D_TEST_EXPR(d_dyn_array_get_size(&root->options) == 0);
    clp_cleanup(root);
}

/* a duplicate stops the insertion: err is filled, earlier entries stay */
static void test_insert_opts_stops_on_duplicate(void)
{
    clp_error_t    e;
    bool           a = false, b = false, c = false;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);

    clp_option_info_t opts[] = {
        {"alpha", "a", NULL, &a, CLP_OPT_ACT_SET, CLP_TYPE_BOOL, false, false, NULL, NULL},
        {"beta", "a", NULL, &b, CLP_OPT_ACT_SET, CLP_TYPE_BOOL, false, false, NULL, NULL},
        {"gamma", "g", NULL, &c, CLP_OPT_ACT_SET, CLP_TYPE_BOOL, false, false, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_opts(root, opts, sizeof(opts) / sizeof(*opts), &e) == CLP_ERR_DUPLICATE);
    D_TEST_EXPR(e.code == CLP_ERR_DUPLICATE);
    D_TEST_NOT_NULL(strstr(e.message, "'-a'"));
    D_TEST_EXPR(d_dyn_array_get_size(&root->options) == 1);
    D_TEST_NOT_NULL(clp_get_option_by_long(root, D_STRING_VIEW_FROM_LITERAL("alpha")));
    D_TEST_EXPR(clp_get_option_by_long(root, D_STRING_VIEW_FROM_LITERAL("gamma")) == NULL);
    clp_cleanup(root);
}

/* an entry clp_new_option_raw rejects stops the insertion with its status */
static void test_insert_opts_stops_on_invalid_entry(void)
{
    clp_error_t    e;
    long           n    = 0;
    usize          v    = 0;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);

    clp_option_info_t bad_name[] = {
        {"num", "n", NULL, &n, CLP_OPT_ACT_SET, CLP_TYPE_LONG, false, false, NULL, NULL},
        {"bad name", NULL, NULL, &n, CLP_OPT_ACT_SET, CLP_TYPE_LONG, false, false, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_opts(root, bad_name, 2, &e) == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(e.code == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(d_dyn_array_get_size(&root->options) == 1);

    clp_option_info_t bad_type[] = {
        {"verbose", "v", NULL, &v, CLP_OPT_ACT_COUNT, CLP_TYPE_LONG, false, false, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_opts(root, bad_type, 1, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(e.code == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(d_dyn_array_get_size(&root->options) == 1);
    clp_cleanup(root);
}

/* ── dest binding (real typed macros, caller-owned variables) ───────────── */

/* values land in the caller's variables; absent options keep their defaults */
static void test_dest_macros_bind_and_keep_defaults(void)
{
    bool            verbose = false;
    long            jobs    = 4;
    usize           port    = 8080;
    double          ratio   = 0.5;
    char            sep     = ',';
    d_string_view_t name    = D_STRING_VIEW_FROM_LITERAL("anon");
    clp_option_t   *o_verbose, *o_jobs, *o_port, *o_ratio, *o_sep, *o_name;
    clp_command_t  *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);

    o_verbose = T_NEW(clp_option_t, clp_new_option, "verbose", "v", NULL, &verbose, false, false);
    o_jobs    = T_NEW(clp_option_t, clp_new_option, "jobs", "j", NULL, &jobs, false, false);
    o_port    = T_NEW(clp_option_t, clp_new_option, "port", "p", NULL, &port, false, false);
    o_ratio   = T_NEW(clp_option_t, clp_new_option, "ratio", "r", NULL, &ratio, false, false);
    o_sep     = T_NEW(clp_option_t, clp_new_option, "sep", "s", NULL, &sep, false, false);
    o_name    = T_NEW(clp_option_t, clp_new_option, "name", "n", NULL, &name, false, false);
    D_TEST_EXPR(o_verbose->type == CLP_TYPE_BOOL && o_jobs->type == CLP_TYPE_LONG && o_port->type == CLP_TYPE_USIZE);
    D_TEST_EXPR(o_ratio->type == CLP_TYPE_DOUBLE && o_sep->type == CLP_TYPE_CHAR && o_name->type == CLP_TYPE_STR);
    T_ADD(clp_add_command_option, root, o_verbose);
    T_ADD(clp_add_command_option, root, o_jobs);
    T_ADD(clp_add_command_option, root, o_port);
    T_ADD(clp_add_command_option, root, o_ratio);
    T_ADD(clp_add_command_option, root, o_sep);
    T_ADD(clp_add_command_option, root, o_name);

    char          *argv[] = {"prog", "-v", "--jobs", "12", "-r", "2.5", "--name=bob", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, NULL) == CLP_OK);
    D_TEST_EXPR(verbose == true && o_verbose->value_set);
    D_TEST_EXPR(jobs == 12 && o_jobs->value_set);
    D_TEST_EXPR(ratio == 2.5 && o_ratio->value_set);
    D_TEST_EXPR(d_string_view_compare_against_c_string(name, "bob") && o_name->value_set);
    D_TEST_EXPR(port == 8080 && o_port->value_set == false);
    D_TEST_EXPR(sep == ',' && o_sep->value_set == false);
    clp_cleanup(root);
}

/* a value that fails conversion or the checker never overwrites dest */
static void test_dest_untouched_on_rejected_value(void)
{
    long           num = 7;
    usize          op  = 42;
    clp_option_t  *o_num;
    clp_operand_t *o_op;
    clp_error_t    e;
    clp_command_t *cmd;

    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    o_num               = T_NEW(clp_option_t, clp_new_option, "num", "n", NULL, &num, false, false);
    T_ADD(clp_add_command_option, root, o_num);
    char *partial[] = {"prog", "-n", "12x", NULL}; /* strtol would have parsed 12 */
    cmd             = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, partial, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(num == 7 && o_num->value_set == false);
    clp_cleanup(root);

    root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    o_num = T_NEW(clp_option_t, clp_new_option_checked, "num", "n", NULL, &num, false, false, check_positive_long);
    T_ADD(clp_add_command_option, root, o_num);
    char *negative[] = {"prog", "-n", "-3", NULL};
    cmd              = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, negative, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(num == 7);
    clp_cleanup(root);

    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    o_op = T_NEW(clp_operand_t, clp_new_operand_checked, "port", NULL, &op, true, check_port_range);
    T_ADD(clp_add_command_operand, root, o_op);
    char *too_big[] = {"prog", "70000", NULL};
    cmd             = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, too_big, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(op == 42 && o_op->value_set == false);
    clp_cleanup(root);
}

/* count, list and kv write into caller-owned counters and collections */
static void test_dest_count_list_kv(void)
{
    usize             level = 1; /* counting starts from the caller's value */
    d_dyn_array_t     tags;
    d_dyn_array_t     files;
    d_unordered_map_t env;
    D_TEST_EXPR(d_dyn_array_init(&tags, sizeof(d_string_view_t), 1, NULL, NULL, RAW_BUF_OPT_NONE) == D_OK);
    D_TEST_EXPR(d_dyn_array_init(&files, sizeof(d_string_view_t), 1, NULL, NULL, RAW_BUF_OPT_NONE) == D_OK);
    D_TEST_EXPR(d_unordered_map_init_not_owned_d_string_view_key(&env, sizeof(d_string_view_t), 1, NULL) == 0);

    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_count, "level", "l", NULL, &level, false));
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_list, "tag", "t", NULL, &tags, false, false));
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_kv, "env", "e", NULL, &env, false, false));
    T_ADD(clp_add_command_operand, root, T_NEW(clp_operand_t, clp_new_operand_list, "files", NULL, &files, true));

    char          *argv[] = {"prog", "-ll", "-t", "a,b", "--env", "K=v", "x", "y", "-l", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, NULL) == CLP_OK);
    D_TEST_EXPR(level == 4);
    D_TEST_EXPR(d_dyn_array_get_size(&tags) == 2);
    D_TEST_EXPR(d_dyn_array_get_size(&files) == 2);
    d_string_view_t key = D_STRING_VIEW_FROM_LITERAL("K");
    D_TEST_NOT_NULL(d_unordered_map_get(&env, &key));
    clp_cleanup(root);

    /* clp_cleanup leaves the caller's collections alone */
    D_TEST_EXPR(d_dyn_array_get_size(&tags) == 2);
    d_dyn_array_destroy(&tags);
    d_dyn_array_destroy(&files);
    d_unordered_map_destroy(&env);
}

/* ── transform (CLP_TYPE_CUSTOM, dest is the ctx) ───────────────────────────────────── */

typedef enum
{
    TS_NONE,
    TS_ONLY,
    TS_ADDR
} ts_type_t;

static ts_type_t   ts_seen;
static int         transform_calls;
static const char *last_transform_arg;

static const char *parse_ts(void *ctx, const char *arg)
{
    ts_type_t *out = ctx;
    transform_calls++;
    last_transform_arg = arg;
    if (strcmp(arg, "tsonly") == 0)
        *out = TS_ONLY;
    else if (strcmp(arg, "tsaddr") == 0)
        *out = TS_ADDR;
    else
        return "expected \"tsonly\" or \"tsaddr\"";
    return NULL;
}

/* the transform gets the raw argument, in every spelling, and value_set follows it */
static void test_option_transform_receives_raw_arg(void)
{
    char *forms[][4] = {
        {"prog", "--ip-timestamp", "tsonly", NULL},
        {"prog", "--ip-timestamp=tsaddr", NULL, NULL},
        {"prog", "-ttsonly", NULL, NULL},
        {"prog", "-t", "tsaddr", NULL},
    };
    ts_type_t expected[] = {TS_ONLY, TS_ADDR, TS_ONLY, TS_ADDR};
    for (usize i = 0; i < 4; i++)
    {
        clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
        clp_option_t  *ts   = T_NEW(clp_option_t, clp_new_option_transform, "ip-timestamp", "t", NULL, parse_ts, &ts_seen, false, false);
        D_TEST_EXPR(ts->type == CLP_TYPE_CUSTOM && ts->has_args && ts->dest == &ts_seen && ts->checker == NULL);
        T_ADD(clp_add_command_option, root, ts);
        clp_command_t *cmd = NULL;
        ts_seen            = TS_NONE;
        transform_calls    = 0;
        D_TEST_EXPR(clp_try_parse_args(root, forms[i], &cmd, NULL) == CLP_OK);
        D_TEST_EXPR(transform_calls == 1 && ts_seen == expected[i] && ts->value_set);
        clp_cleanup(root);
    }
}

/* a rejected transform fails the parse with its message; absent → never called */
static void test_option_transform_rejects_and_absent(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *ts   = T_NEW(clp_option_t, clp_new_option_transform, "ip-timestamp", NULL, NULL, parse_ts, &ts_seen, false, false);
    T_ADD(clp_add_command_option, root, ts);
    char          *bad[] = {"prog", "--ip-timestamp", "tsfoo", NULL};
    clp_command_t *cmd   = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, bad, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "'tsfoo'"));
    D_TEST_NOT_NULL(strstr(e.message, "expected \"tsonly\" or \"tsaddr\""));
    D_TEST_EXPR(ts->value_set == false);
    clp_cleanup(root);

    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    ts   = T_NEW(clp_option_t, clp_new_option_transform, "ip-timestamp", NULL, NULL, parse_ts, &ts_seen, false, false);
    T_ADD(clp_add_command_option, root, ts);
    char *absent[]  = {"prog", NULL};
    char *missing[] = {"prog", "--ip-timestamp", NULL};
    transform_calls = 0;
    cmd             = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, absent, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(transform_calls == 0 && ts->value_set == false);
    D_TEST_EXPR(clp_try_parse_args(root, missing, &cmd, &e) == CLP_ERR_MISSING_ARG);
    D_TEST_EXPR(transform_calls == 0);
    clp_cleanup(root);
}

/* required transform option is enforced through value_set */
static void test_option_transform_required(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_transform, "ip-timestamp", NULL, NULL, parse_ts, &ts_seen, true, false));
    char          *argv[] = {"prog", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_MISSING_REQUIRED);
    clp_cleanup(root);
}

/* operand transform: called with the positional argument */
static void test_operand_transform(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_operand_t *mode = T_NEW(clp_operand_t, clp_new_operand_transform, "mode", NULL, parse_ts, &ts_seen, true);
    T_ADD(clp_add_command_operand, root, mode);
    char          *ok[] = {"prog", "tsaddr", NULL};
    clp_command_t *cmd  = NULL;
    ts_seen             = TS_NONE;
    D_TEST_EXPR(clp_try_parse_args(root, ok, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(ts_seen == TS_ADDR && mode->value_set);
    D_TEST_EXPR(strcmp(last_transform_arg, "tsaddr") == 0);
    clp_cleanup(root);

    root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    mode = T_NEW(clp_operand_t, clp_new_operand_transform, "mode", NULL, parse_ts, &ts_seen, true);
    T_ADD(clp_add_command_operand, root, mode);
    char *bad[] = {"prog", "nope", NULL};
    cmd         = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, bad, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "<mode>"));
    D_TEST_EXPR(mode->value_set == false);
    clp_cleanup(root);
}

/* transform <=> CLP_TYPE_CUSTOM, SET only, no checker: everything else is refused */
static void test_transform_invalid_combinations(void)
{
    clp_option_t  *o;
    clp_operand_t *op;
    clp_error_t    e;
    long           n;
    D_TEST_EXPR(clp_new_option_raw(&o, "x", NULL, NULL, &n, CLP_OPT_ACT_SET, CLP_TYPE_LONG, false, false, NULL, parse_ts, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_NULL(o);
    D_TEST_EXPR(clp_new_option_raw(&o, "x", NULL, NULL, NULL, CLP_OPT_ACT_SET, CLP_TYPE_CUSTOM, false, false, NULL, NULL, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(clp_new_option_raw(&o, "x", NULL, NULL, NULL, CLP_OPT_ACT_LIST, CLP_TYPE_CUSTOM, false, false, NULL, parse_ts, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(clp_new_option_raw(&o, "x", NULL, NULL, NULL, CLP_OPT_ACT_SET, CLP_TYPE_CUSTOM, false, false, check_no_dot_str, parse_ts, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_NOT_NULL(strstr(e.message, "checker"));
    D_TEST_EXPR(clp_new_operand_raw(&op, "x", NULL, NULL, CLP_OPERAND_ACT_KV, CLP_TYPE_CUSTOM, false, NULL, parse_ts, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_NULL(op);
}

/* --help shows a transform option as <value> */
static void test_transform_help_shows_value(void)
{
    int pipefd[2];
    D_TEST_EXPR(pipe(pipefd) == 0);
    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
        T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_transform, "ip-timestamp", NULL, NULL, parse_ts, &ts_seen, false, false));
        char          *argv[] = {"prog", "--help", NULL};
        clp_command_t *cmd    = NULL;
        clp_parse_args(root, argv, &cmd);
        _exit(1);
    }
    close(pipefd[1]);
    char    buf[4096] = {0};
    ssize_t n, total = 0;
    while ((n = read(pipefd[0], buf + total, sizeof(buf) - 1 - total)) > 0)
        total += n;
    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);
    D_TEST_NOT_NULL(strstr(buf, "--ip-timestamp <value>"));
}

/* regression: bool flags take no argument, whatever comes next in argv */
static void test_bool_flag_with_and_without_following_token(void)
{
    char *forms[][4] = {
        {"prog", "-v", NULL, NULL},
        {"prog", "--verbose", NULL, NULL},
        {"prog", "--verbose", "file", NULL},
        {"prog", "-v", "file", NULL},
    };
    for (usize i = 0; i < 4; i++)
    {
        bool            verbose = false;
        d_string_view_t file    = {0};
        clp_command_t  *root    = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
        T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option, "verbose", "v", NULL, &verbose, false, false));
        T_ADD(clp_add_command_operand, root, T_NEW(clp_operand_t, clp_new_operand, "file", NULL, &file, false));
        clp_command_t *cmd = NULL;
        D_TEST_EXPR(clp_try_parse_args(root, forms[i], &cmd, NULL) == CLP_OK);
        D_TEST_EXPR(verbose == true);
        D_TEST_EXPR(forms[i][2] == NULL || d_string_view_compare_against_c_string(file, "file"));
        clp_cleanup(root);
    }
}

/* regression: a conversion failure is reported, not silently accepted */
static void test_conversion_failure_is_reported(void)
{
    long           ttl = 64;
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option, "ttl", NULL, NULL, &ttl, false, false));
    char          *argv[] = {"prog", "--ttl", "abc", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_NOT_NULL(strstr(e.message, "'abc'"));
    D_TEST_EXPR(ttl == 64);
    clp_cleanup(root);
}

/* one transform shared by two options: each result goes to its own ctx */
static void test_transform_ctx_shared_by_two_options(void)
{
    clp_error_t    e;
    ts_type_t      in = TS_NONE, out = TS_NONE;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_transform, "in", NULL, NULL, parse_ts, &in, false, false));
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_transform, "out", NULL, NULL, parse_ts, &out, false, false));
    char          *argv[] = {"prog", "--in", "tsonly", "--out=tsaddr", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(in == TS_ONLY && out == TS_ADDR);
    clp_cleanup(root);
}

/* a NULL ctx is passed through as is */
static void *seen_ctx = (void *) 1;
static const char *record_ctx(void *ctx, const char *arg)
{
    (void) arg;
    seen_ctx = ctx;
    return NULL;
}

static void test_transform_null_ctx(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    T_ADD(clp_add_command_operand, root, T_NEW(clp_operand_t, clp_new_operand_transform, "x", NULL, record_ctx, NULL, true));
    char          *argv[] = {"prog", "v", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_NULL(seen_ctx);
    clp_cleanup(root);
}

/* ── list / kv init helpers ─────────────────────────────────────────────── */

static void test_list_and_kv_init_helpers(void)
{
    clp_error_t       e;
    d_dyn_array_t     tags;
    d_unordered_map_t env;
    D_TEST_EXPR(clp_list_init(&tags, &e) == CLP_OK);
    D_TEST_EXPR(clp_kv_init(&env, &e) == CLP_OK);
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_list, "tag", "t", NULL, &tags, false, false));
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option_kv, "env", "e", NULL, &env, false, false));
    char          *argv[] = {"prog", "-t", "a,b", "--tag=c", "-e", "K=V,X=Y", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(d_dyn_array_get_size(&tags) == 3);
    d_string_view_t first = {0};
    D_TEST_EXPR(d_dyn_array_get_elem_at(&tags, 0, &first) == D_OK && d_string_view_compare_against_c_string(first, "a"));
    d_string_view_t  key   = D_STRING_VIEW_FROM_LITERAL("X");
    d_string_view_t *value = d_unordered_map_get(&env, &key);
    D_TEST_EXPR(value != NULL && d_string_view_compare_against_c_string(*value, "Y"));
    clp_cleanup(root);
    clp_list_destroy(&tags);
    clp_kv_destroy(&env);
}

/* ── global options looked up in ancestors ──────────────────────────────── */

static clp_command_t *build_global_tree(clp_command_t **sub_out, clp_option_t **verbose_out, clp_option_t **token_out, bool *verbose, d_string_view_t *token, bool token_required)
{
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_command_t *sub  = T_NEW(clp_command_t, clp_new_command, 1, "sub", NULL, NULL, NULL);
    clp_option_t  *v    = T_NEW(clp_option_t, clp_new_option, "verbose", "v", NULL, verbose, false, true);
    clp_option_t  *t    = T_NEW(clp_option_t, clp_new_option, "token", "T", NULL, token, token_required, true);
    T_ADD(clp_add_command_option, root, v);
    T_ADD(clp_add_command_option, root, t);
    T_ADD(clp_add_command_sub_command, root, sub);
    *sub_out     = sub;
    *verbose_out = v;
    *token_out   = t;
    return root;
}

/* a global option is accepted after the subcommand name, long and short */
static void test_global_option_after_subcommand_name(void)
{
    char *forms[][4] = {
        {"prog", "sub", "--verbose", NULL},
        {"prog", "sub", "-v", NULL},
        {"prog", "-v", "sub", NULL},
    };
    for (usize i = 0; i < 3; i++)
    {
        clp_error_t     e;
        bool            verbose = false;
        d_string_view_t token   = {0};
        clp_command_t  *sub;
        clp_option_t   *v, *t;
        clp_command_t  *root = build_global_tree(&sub, &v, &t, &verbose, &token, false);
        clp_command_t  *cmd  = NULL;
        D_TEST_EXPR(clp_try_parse_args(root, forms[i], &cmd, &e) == CLP_OK);
        D_TEST_EXPR(cmd == sub && verbose == true && v->value_set);
        clp_cleanup(root);
    }
}

/* regression: globals are no longer copied into the child, so cleanup frees
 * each option once (this used to double free; run under valgrind) */
static void test_global_option_not_copied_into_child(void)
{
    clp_error_t     e;
    bool            verbose = false;
    d_string_view_t token   = {0};
    clp_command_t  *sub;
    clp_option_t   *v, *t;
    clp_command_t  *root   = build_global_tree(&sub, &v, &t, &verbose, &token, false);
    char           *argv[] = {"prog", "sub", "--", NULL};
    clp_command_t  *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(d_dyn_array_get_size(&sub->options) == 0);
    D_TEST_EXPR(clp_get_option_by_long(sub, D_STRING_VIEW_FROM_LITERAL("verbose")) == NULL);
    clp_cleanup(root);
}

/* a required global is enforced on the subcommand even when it gets no args */
static void test_required_global_checked_when_child_has_no_args(void)
{
    clp_error_t     e;
    bool            verbose = false;
    d_string_view_t token   = {0};
    clp_command_t  *sub;
    clp_option_t   *v, *t;
    clp_command_t  *root   = build_global_tree(&sub, &v, &t, &verbose, &token, true);
    char           *argv[] = {"prog", "sub", NULL};
    clp_command_t  *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_MISSING_REQUIRED);
    D_TEST_NOT_NULL(strstr(e.message, "--token"));
    D_TEST_EXPR(e.command == sub);
    char *ok[] = {"prog", "sub", "--token", "abc", NULL};
    D_TEST_EXPR(clp_try_parse_args(root, ok, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(token, "abc"));
    clp_cleanup(root);
}

/* a subcommand's own option shadows an ancestor global with the same name */
static void test_child_option_shadows_parent_global(void)
{
    clp_error_t     e;
    bool            verbose = false, child_verbose = false;
    d_string_view_t token = {0};
    clp_command_t  *sub;
    clp_option_t   *v, *t;
    clp_command_t  *root = build_global_tree(&sub, &v, &t, &verbose, &token, false);
    T_ADD(clp_add_command_option, sub, T_NEW(clp_option_t, clp_new_option, "verbose", "v", NULL, &child_verbose, false, false));
    char          *argv[] = {"prog", "sub", "-v", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(child_verbose == true && verbose == false);
    clp_cleanup(root);
}

/* a non-global parent option stays unknown in the subcommand */
static void test_non_global_parent_option_unknown_in_child(void)
{
    clp_error_t    e;
    bool           local = false;
    clp_command_t *root  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_command_t *sub   = T_NEW(clp_command_t, clp_new_command, 1, "sub", NULL, NULL, NULL);
    T_ADD(clp_add_command_option, root, T_NEW(clp_option_t, clp_new_option, "local", "l", NULL, &local, false, false));
    T_ADD(clp_add_command_sub_command, root, sub);
    char          *argv[] = {"prog", "sub", "--local", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_UNKNOWN_OPTION);
    clp_cleanup(root);
}

/* every operand entry is created, registered in order and bound to its dest */
static void test_insert_operands_creates_and_registers(void)
{
    clp_error_t     e;
    d_string_view_t host = D_STRING_VIEW_FROM_LITERAL("none");
    usize           port = 0;
    d_dyn_array_t   rest;
    clp_command_t  *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    D_TEST_EXPR(clp_list_init(&rest, &e) == CLP_OK);

    clp_operand_info_t ops[] = {
        {"host", "host to reach", &host, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, true, NULL, NULL},
        {"port", NULL, &port, CLP_OPERAND_ACT_SET, CLP_TYPE_USIZE, false, check_port_range, NULL},
        {"rest", NULL, &rest, CLP_OPERAND_ACT_LIST, CLP_TYPE_STR, false, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_operands(root, ops, sizeof(ops) / sizeof(*ops), &e) == CLP_OK);
    D_TEST_EXPR(d_dyn_array_get_size(&root->operands) == 3);
    d_dyn_array_t *operands = &root->operands;
    clp_operand_t *o_port   = d_dyn_array_get_elem_deref_addr_at_safe(operands, 1);
    D_TEST_EXPR(o_port->dest == &port && o_port->checker == check_port_range && o_port->required == false);

    char          *argv[] = {"prog", "example.com", "443", "x", "y", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(d_string_view_compare_against_c_string(host, "example.com"));
    D_TEST_EXPR(port == 443);
    D_TEST_EXPR(d_dyn_array_get_size(&rest) == 2);
    clp_cleanup(root);
    clp_list_destroy(&rest);
}

/* count 0 is a no-op, infos may then be NULL */
static void test_insert_operands_empty(void)
{
    clp_error_t    e;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    D_TEST_EXPR(clp_insert_operands(root, NULL, 0, &e) == CLP_OK);
    D_TEST_EXPR(d_dyn_array_get_size(&root->operands) == 0);
    clp_cleanup(root);
}

/* a required operand after an optional one stops the insertion; earlier entries stay */
static void test_insert_operands_stops_on_order_error(void)
{
    clp_error_t     e;
    d_string_view_t a = {0}, b = {0}, c = {0};
    clp_command_t  *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);

    clp_operand_info_t ops[] = {
        {"first", NULL, &a, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, false, NULL, NULL},
        {"second", NULL, &b, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, true, NULL, NULL},
        {"third", NULL, &c, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, false, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_operands(root, ops, sizeof(ops) / sizeof(*ops), &e) == CLP_ERR_OPERAND_ORDER);
    D_TEST_EXPR(e.code == CLP_ERR_OPERAND_ORDER);
    D_TEST_NOT_NULL(strstr(e.message, "'second'"));
    D_TEST_EXPR(d_dyn_array_get_size(&root->operands) == 1);
    clp_cleanup(root);
}

/* an entry clp_new_operand_raw rejects stops the insertion with its status */
static void test_insert_operands_stops_on_invalid_entry(void)
{
    clp_error_t     e;
    d_string_view_t s    = {0};
    usize           n    = 0;
    clp_command_t  *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);

    clp_operand_info_t bad_name[] = {
        {"file", NULL, &s, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, true, NULL, NULL},
        {"", NULL, &s, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, true, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_operands(root, bad_name, 2, &e) == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(e.code == CLP_ERR_INVALID_NAME);
    D_TEST_EXPR(d_dyn_array_get_size(&root->operands) == 1);

    clp_operand_info_t bad_type[] = {
        {"num", NULL, &n, CLP_OPERAND_ACT_SET, CLP_TYPE_USIZE, true, check_port_range, parse_ts},
    };
    D_TEST_EXPR(clp_insert_operands(root, bad_type, 1, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(d_dyn_array_get_size(&root->operands) == 1);
    clp_cleanup(root);
}

/* operands cannot go on a command that already has subcommands */
static void test_insert_operands_conflict_with_subcommands(void)
{
    clp_error_t     e;
    d_string_view_t s    = {0};
    clp_command_t  *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_command_t  *sub  = T_NEW(clp_command_t, clp_new_command, 0, "sub", NULL, NULL, NULL);
    T_ADD(clp_add_command_sub_command, root, sub);

    clp_operand_info_t ops[] = {
        {"file", NULL, &s, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, true, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_operands(root, ops, 1, &e) == CLP_ERR_CONFLICT);
    D_TEST_EXPR(d_dyn_array_get_size(&root->operands) == 0);
    clp_cleanup(root);
}

/* ── command validator (post-parse hook) ────────────────────────────────── */

typedef struct
{
    int           calls;
    clp_option_t *flood;
    clp_option_t *interval;
} validator_ctx_t;

/* ping-like rule: --flood and --interval are mutually exclusive */
static const char *validate_flood_interval(void *ctx)
{
    validator_ctx_t *v = ctx;
    v->calls++;
    if (v->flood->value_set && v->interval->value_set)
        return "-f and -i are incompatible";
    return NULL;
}

static clp_command_t *make_validated_root(validator_ctx_t *v, bool *flood, long *interval)
{
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "ping", NULL, validate_flood_interval, v);
    v->flood            = T_NEW(clp_option_t, clp_new_option, "flood", "f", NULL, flood, false, false);
    v->interval         = T_NEW(clp_option_t, clp_new_option, "interval", "i", NULL, interval, false, false);
    T_ADD(clp_add_command_option, root, v->flood);
    T_ADD(clp_add_command_option, root, v->interval);
    return root;
}

/* the constructor stores validator and ctx */
static void test_validator_stored_by_constructor(void)
{
    validator_ctx_t v     = {0};
    clp_command_t  *plain = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_command_t  *hook  = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, validate_flood_interval, &v);
    D_TEST_EXPR(plain->validator == NULL && plain->ctx == NULL);
    D_TEST_EXPR(hook->validator == validate_flood_interval && hook->ctx == &v);
    clp_cleanup(plain);
    clp_cleanup(hook);
}

/* accepted: runs once, after values were bound, and parsing succeeds */
static void test_validator_accepts(void)
{
    clp_error_t     e;
    validator_ctx_t v        = {0};
    bool            flood    = false;
    long            interval = 1;
    clp_command_t  *root     = make_validated_root(&v, &flood, &interval);
    char           *argv[]   = {"ping", "-i", "5", NULL};
    clp_command_t  *cmd      = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(v.calls == 1);
    D_TEST_EXPR(interval == 5 && flood == false);
    clp_cleanup(root);
}

/* rejected: CLP_ERR_VALIDATOR, the command, and its message with the command name */
static void test_validator_rejects(void)
{
    clp_error_t     e;
    validator_ctx_t v        = {0};
    bool            flood    = false;
    long            interval = 1;
    clp_command_t  *root     = make_validated_root(&v, &flood, &interval);
    char           *argv[]   = {"ping", "-f", "-i", "5", NULL};
    clp_command_t  *cmd      = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_VALIDATOR);
    D_TEST_EXPR(v.calls == 1);
    D_TEST_EXPR(e.code == CLP_ERR_VALIDATOR && e.command == root);
    D_TEST_EXPR(strcmp(e.message, "command ping: -f and -i are incompatible\n") == 0);
    clp_cleanup(root);
}

/* runs even when the command gets no arguments at all */
static void test_validator_runs_without_args(void)
{
    clp_error_t     e;
    validator_ctx_t v        = {0};
    bool            flood    = false;
    long            interval = 1;
    clp_command_t  *root     = make_validated_root(&v, &flood, &interval);
    char           *argv[]   = {"ping", NULL};
    clp_command_t  *cmd      = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(v.calls == 1);
    clp_cleanup(root);
}

/* not run when parsing already failed, nor when a required argument is missing */
static void test_validator_skipped_on_earlier_error(void)
{
    clp_error_t     e;
    validator_ctx_t v        = {0};
    bool            flood    = false;
    long            interval = 1;
    d_string_view_t host     = {0};
    clp_command_t  *root     = make_validated_root(&v, &flood, &interval);
    clp_operand_t  *op       = T_NEW(clp_operand_t, clp_new_operand, "host", NULL, &host, true);
    T_ADD(clp_add_command_operand, root, op);
    clp_command_t *cmd = NULL;

    char *bad_value[] = {"ping", "-i", "x", "h", NULL};
    D_TEST_EXPR(clp_try_parse_args(root, bad_value, &cmd, &e) == CLP_ERR_BAD_VALUE);
    char *missing[] = {"ping", "-f", NULL};
    D_TEST_EXPR(clp_try_parse_args(root, missing, &cmd, &e) == CLP_ERR_MISSING_REQUIRED);
    D_TEST_EXPR(v.calls == 0);
    clp_cleanup(root);
}

/* only the validator of the command parsing ends on runs */
static void test_validator_only_on_matched_command(void)
{
    clp_error_t     e;
    validator_ctx_t root_v = {0}, sub_v = {0};
    bool            flood = false, sub_flood = false;
    long            interval = 1, sub_interval = 1;
    clp_command_t  *root = make_validated_root(&root_v, &flood, &interval);
    clp_command_t  *sub  = T_NEW(clp_command_t, clp_new_command, 1, "sub", NULL, validate_flood_interval, &sub_v);
    sub_v.flood          = T_NEW(clp_option_t, clp_new_option, "flood", "f", NULL, &sub_flood, false, false);
    sub_v.interval       = T_NEW(clp_option_t, clp_new_option, "interval", "i", NULL, &sub_interval, false, false);
    T_ADD(clp_add_command_option, sub, sub_v.flood);
    T_ADD(clp_add_command_option, sub, sub_v.interval);
    T_ADD(clp_add_command_sub_command, root, sub);

    char          *argv[] = {"ping", "sub", "-f", "-i", "2", NULL};
    clp_command_t *cmd    = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_ERR_VALIDATOR);
    D_TEST_EXPR(cmd == sub && e.command == sub);
    D_TEST_NOT_NULL(strstr(e.message, "command sub: "));
    D_TEST_EXPR(sub_v.calls == 1 && root_v.calls == 0);
    clp_cleanup(root);
}

/* a NULL err from the caller is fine: only the status comes back */
static void test_validator_with_null_err(void)
{
    validator_ctx_t v        = {0};
    bool            flood    = false;
    long            interval = 1;
    clp_command_t  *root     = make_validated_root(&v, &flood, &interval);
    char           *argv[]   = {"ping", "-f", "-i", "5", NULL};
    clp_command_t  *cmd      = NULL;
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, NULL) == CLP_ERR_VALIDATOR);
    D_TEST_EXPR(v.calls == 1);
    clp_cleanup(root);
}

/* ── CLP_OPT_ACT_TRIGGER: custom options without an argument ────────────── */

static int         flag_calls;
static const char *flag_last_arg;

/* counts its calls through ctx; rejects once ctx reaches 3 */
static const char *bump_flag(void *ctx, const char *arg)
{
    int *n = ctx;
    flag_calls++;
    flag_last_arg = arg;
    if (++*n >= 3)
        return "given too many times";
    return NULL;
}

/* trigger requires CLP_TYPE_CUSTOM and a transform; it never takes an argument */
static void test_trigger_rules(void)
{
    clp_error_t   e;
    clp_option_t *o   = NULL;
    long          n   = 0;
    int           ctx = 0;

    D_TEST_EXPR(clp_new_option_raw(&o, "num", NULL, NULL, &n, CLP_OPT_ACT_TRIGGER, CLP_TYPE_LONG, false, false, NULL, NULL, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(o == NULL && e.code == CLP_ERR_INVALID_TYPE);
    D_TEST_NOT_NULL(strstr(e.message, "action 'trigger' requires type 'custom'"));
    D_TEST_EXPR(clp_new_option_raw(&o, "f", NULL, NULL, &ctx, CLP_OPT_ACT_TRIGGER, CLP_TYPE_CUSTOM, false, false, NULL, NULL, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_NOT_NULL(strstr(e.message, "type 'custom' requires a transform"));
    D_TEST_EXPR(clp_new_option_raw(&o, "f", NULL, NULL, &ctx, CLP_OPT_ACT_TRIGGER, CLP_TYPE_CUSTOM, false, false, check_no_dot_str, bump_flag, &e) == CLP_ERR_INVALID_TYPE);

    D_TEST_EXPR(clp_new_option_raw(&o, "f", NULL, NULL, &ctx, CLP_OPT_ACT_TRIGGER, CLP_TYPE_CUSTOM, false, false, NULL, bump_flag, &e) == CLP_OK);
    D_TEST_EXPR(o->action == CLP_OPT_ACT_TRIGGER && o->has_args == false && o->dest == &ctx);
    clp_free_option(o);

    /* the macro builds the same thing; a SET transform still takes an argument */
    o = T_NEW(clp_option_t, clp_new_option_transform_flag, "flood", "f", NULL, bump_flag, &ctx, false, false);
    D_TEST_EXPR(o->action == CLP_OPT_ACT_TRIGGER && o->type == CLP_TYPE_CUSTOM && o->has_args == false);
    clp_free_option(o);
    o = T_NEW(clp_option_t, clp_new_option_transform, "mode", "m", NULL, bump_flag, &ctx, false, false);
    D_TEST_EXPR(o->action == CLP_OPT_ACT_SET && o->has_args == true);
    clp_free_option(o);
}

/* a trigger runs with arg == NULL each time and consumes no argv */
static void test_trigger_parses(void)
{
    clp_error_t     e;
    int             seen = 0;
    bool            v    = false;
    d_string_view_t host = {0};
    clp_command_t  *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t   *f    = T_NEW(clp_option_t, clp_new_option_transform_flag, "flood", "f", NULL, bump_flag, &seen, false, false);
    clp_option_t   *vo   = T_NEW(clp_option_t, clp_new_option, "verbose", "v", NULL, &v, false, false);
    clp_operand_t  *op   = T_NEW(clp_operand_t, clp_new_operand, "host", NULL, &host, true);
    T_ADD(clp_add_command_option, root, f);
    T_ADD(clp_add_command_option, root, vo);
    T_ADD(clp_add_command_operand, root, op);

    char          *argv[] = {"prog", "--flood", "example.com", "-fv", NULL};
    clp_command_t *cmd    = NULL;
    flag_calls            = 0;
    flag_last_arg         = "unset";
    D_TEST_EXPR(clp_try_parse_args(root, argv, &cmd, &e) == CLP_OK);
    D_TEST_EXPR(flag_calls == 2 && seen == 2 && flag_last_arg == NULL);
    D_TEST_EXPR(f->value_set == true && v == true);
    D_TEST_EXPR(d_string_view_compare_against_c_string(host, "example.com"));
    clp_cleanup(root);
}

/* a trigger rejects an inline value, and its own rejection is BAD_VALUE */
static void test_trigger_errors(void)
{
    clp_error_t    e;
    int            seen = 0;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);
    clp_option_t  *f    = T_NEW(clp_option_t, clp_new_option_transform_flag, "flood", "f", NULL, bump_flag, &seen, false, false);
    T_ADD(clp_add_command_option, root, f);
    clp_command_t *cmd = NULL;

    char *inline_value[] = {"prog", "--flood=x", NULL};
    D_TEST_EXPR(clp_try_parse_args(root, inline_value, &cmd, &e) == CLP_ERR_UNEXPECTED_ARG);
    D_TEST_EXPR(seen == 0);

    char *too_many[] = {"prog", "-fff", NULL};
    D_TEST_EXPR(clp_try_parse_args(root, too_many, &cmd, &e) == CLP_ERR_BAD_VALUE);
    D_TEST_EXPR(seen == 3);
    D_TEST_NOT_NULL(strstr(e.message, "'-f': given too many times"));
    clp_cleanup(root);
}

/* clp_insert_opts accepts a trigger row like any other action */
static void test_insert_opts_trigger(void)
{
    clp_error_t    e;
    int            seen = 0;
    long           n    = 0;
    clp_command_t *root = T_NEW(clp_command_t, clp_new_command, 0, "prog", NULL, NULL, NULL);

    clp_option_info_t ok[] = {
        {"flood", "f", NULL, &seen, CLP_OPT_ACT_TRIGGER, CLP_TYPE_CUSTOM, false, false, NULL, bump_flag},
        {"num", "n", NULL, &n, CLP_OPT_ACT_SET, CLP_TYPE_LONG, false, false, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_opts(root, ok, 2, &e) == CLP_OK);
    clp_option_t *f = clp_get_option_by_long(root, D_STRING_VIEW_FROM_LITERAL("flood"));
    D_TEST_NOT_NULL(f);
    D_TEST_EXPR(f->has_args == false);

    clp_option_info_t bad[] = {
        {"ttl", NULL, NULL, &n, CLP_OPT_ACT_TRIGGER, CLP_TYPE_LONG, false, false, NULL, NULL},
    };
    D_TEST_EXPR(clp_insert_opts(root, bad, 1, &e) == CLP_ERR_INVALID_TYPE);
    D_TEST_EXPR(d_dyn_array_get_size(&root->options) == 2);
    clp_cleanup(root);
}

int main(void)
{
    atexit(t_release_dests);
    d_test_t tests[] = {
        /* transform */
        D_TEST_GENERATE_TEST(test_option_transform_receives_raw_arg),
        D_TEST_GENERATE_TEST(test_option_transform_rejects_and_absent),
        D_TEST_GENERATE_TEST(test_option_transform_required),
        D_TEST_GENERATE_TEST(test_operand_transform),
        D_TEST_GENERATE_TEST(test_insert_opts_creates_and_registers),
        D_TEST_GENERATE_TEST(test_insert_opts_empty),
        D_TEST_GENERATE_TEST(test_insert_opts_stops_on_duplicate),
        D_TEST_GENERATE_TEST(test_insert_opts_stops_on_invalid_entry),
        D_TEST_GENERATE_TEST(test_insert_operands_creates_and_registers),
        D_TEST_GENERATE_TEST(test_insert_operands_empty),
        D_TEST_GENERATE_TEST(test_insert_operands_stops_on_order_error),
        D_TEST_GENERATE_TEST(test_insert_operands_stops_on_invalid_entry),
        D_TEST_GENERATE_TEST(test_insert_operands_conflict_with_subcommands),
        D_TEST_GENERATE_TEST(test_validator_stored_by_constructor),
        D_TEST_GENERATE_TEST(test_validator_accepts),
        D_TEST_GENERATE_TEST(test_validator_rejects),
        D_TEST_GENERATE_TEST(test_validator_runs_without_args),
        D_TEST_GENERATE_TEST(test_validator_skipped_on_earlier_error),
        D_TEST_GENERATE_TEST(test_validator_only_on_matched_command),
        D_TEST_GENERATE_TEST(test_validator_with_null_err),
        D_TEST_GENERATE_TEST(test_transform_invalid_combinations),
        D_TEST_GENERATE_TEST(test_transform_help_shows_value),
        D_TEST_GENERATE_TEST(test_bool_flag_with_and_without_following_token),
        D_TEST_GENERATE_TEST(test_conversion_failure_is_reported),
        /* dest binding */
        D_TEST_GENERATE_TEST(test_dest_macros_bind_and_keep_defaults),
        D_TEST_GENERATE_TEST(test_dest_untouched_on_rejected_value),
        D_TEST_GENERATE_TEST(test_dest_count_list_kv),
        /* getters */
        D_TEST_GENERATE_TEST(test_get_option_by_short_finds_registered_option),
        D_TEST_GENERATE_TEST(test_get_option_by_short_returns_null_for_unknown),
        D_TEST_GENERATE_TEST(test_get_option_by_long_finds_registered_option),
        D_TEST_GENERATE_TEST(test_get_option_by_long_returns_null_for_unknown),
        D_TEST_GENERATE_TEST(test_get_operand_finds_registered_operand),
        D_TEST_GENERATE_TEST(test_get_operand_returns_null_for_unknown),
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
        D_TEST_GENERATE_TEST(test_single_operand_is_set),
        D_TEST_GENERATE_TEST(test_options_and_operand_together),
        D_TEST_GENERATE_TEST(test_option_after_operand),
        D_TEST_GENERATE_TEST(test_multiple_operands),
        D_TEST_GENERATE_TEST(test_double_hyphen_terminates_option_parsing),
        D_TEST_GENERATE_TEST(test_double_hyphen_with_options_before),
        D_TEST_GENERATE_TEST(test_list_operand_consumes_remaining_args),
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
        D_TEST_GENERATE_TEST(test_missing_required_operand_exits),
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
        D_TEST_GENERATE_TEST(test_required_operand_after_optional_exits),
        D_TEST_GENERATE_TEST(test_long_opt_name_starting_with_hyphen_exits),
        D_TEST_GENERATE_TEST(test_long_opt_name_starting_with_underscore_exits),
        D_TEST_GENERATE_TEST(test_short_opt_name_non_alnum_exits),
        D_TEST_GENERATE_TEST(test_count_action_with_non_usize_type_exits),
        D_TEST_GENERATE_TEST(test_count_action_short_only_with_non_usize_type_exits),
        D_TEST_GENERATE_TEST(test_parent_option_not_visible_in_subcommand_exits),
        D_TEST_GENERATE_TEST(test_missing_required_option_in_subcommand_exits),
        D_TEST_GENERATE_TEST(test_missing_required_operand_in_subcommand_exits),
        D_TEST_GENERATE_TEST(test_unknown_short_in_combined_token_exits),
        D_TEST_GENERATE_TEST(test_duplicate_operand_name_warns_no_exit),
        D_TEST_GENERATE_TEST(test_builder_duplicate_long_returns_status),
        D_TEST_GENERATE_TEST(test_builder_invalid_names_return_status),
        D_TEST_GENERATE_TEST(test_builder_conflict_and_order_return_status),
        D_TEST_GENERATE_TEST(test_status_to_str_covers_builder_codes),
        D_TEST_GENERATE_TEST(test_long_opt_name_with_invalid_chars_exits),
        D_TEST_GENERATE_TEST(test_add_subcommand_when_operands_exist_exits),
        D_TEST_GENERATE_TEST(test_add_operand_when_subcommands_exist_exits),
        D_TEST_GENERATE_TEST(test_short_opt_missing_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_value_via_short_opt_exits),
        D_TEST_GENERATE_TEST(test_invalid_double_option_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_usize_operand_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_long_operand_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_char_operand_value_exits),
        D_TEST_GENERATE_TEST(test_invalid_double_operand_value_exits),
        /* input checkers */
        D_TEST_GENERATE_TEST(test_option_checker_accepts_valid_value),
        D_TEST_GENERATE_TEST(test_option_checker_rejects_value),
        D_TEST_GENERATE_TEST(test_option_checker_rejects_short_form),
        D_TEST_GENERATE_TEST(test_option_checker_on_string_value),
        D_TEST_GENERATE_TEST(test_option_checker_skipped_on_conversion_error),
        D_TEST_GENERATE_TEST(test_option_checker_not_called_when_absent),
        D_TEST_GENERATE_TEST(test_option_macro_has_no_checker),
        D_TEST_GENERATE_TEST(test_operand_checker_accepts_valid_value),
        D_TEST_GENERATE_TEST(test_operand_checker_rejects_value),
        D_TEST_GENERATE_TEST(test_option_checker_reject_exits),
        D_TEST_GENERATE_TEST(test_option_checked_macro_rejects_value),
        D_TEST_GENERATE_TEST(test_option_default_checked_macro),
        D_TEST_GENERATE_TEST(test_option_default_str_checked_macro),
        D_TEST_GENERATE_TEST(test_operand_checked_macros),
        D_TEST_GENERATE_TEST(test_list_option_checker_checks_each_item),
        D_TEST_GENERATE_TEST(test_list_operand_checker_checks_each_item),
        D_TEST_GENERATE_TEST(test_kv_option_checker_accepts_pairs),
        D_TEST_GENERATE_TEST(test_kv_option_checker_rejects_key),
        D_TEST_GENERATE_TEST(test_kv_option_checker_rejects_value),
        D_TEST_GENERATE_TEST(test_kv_checker_not_called_on_malformed_pair),
        D_TEST_GENERATE_TEST(test_kv_operand_checker),
        D_TEST_GENERATE_TEST(test_multiple_missing_required_options_exits),
        D_TEST_GENERATE_TEST(test_multiple_missing_required_operands_exits),
        /* key=value option and operand */
        D_TEST_GENERATE_TEST(test_kv_option_single_pair),
        D_TEST_GENERATE_TEST(test_kv_option_multiple_pairs),
        D_TEST_GENERATE_TEST(test_kv_operand_single_pair),
        D_TEST_GENERATE_TEST(test_kv_opt_missing_eq_exits),
        D_TEST_GENERATE_TEST(test_kv_opt_empty_key_exits),
        D_TEST_GENERATE_TEST(test_kv_opt_empty_value_exits),
        D_TEST_GENERATE_TEST(test_kv_operand_missing_eq_exits),
        D_TEST_GENERATE_TEST(test_kv_operand_empty_key_exits),
        D_TEST_GENERATE_TEST(test_kv_operand_empty_value_exits),
        /* print_usage */
        D_TEST_GENERATE_TEST(test_usage_line_in_stderr_on_missing_required_option),
        D_TEST_GENERATE_TEST(test_for_more_info_hint_on_missing_required_option),
        D_TEST_GENERATE_TEST(test_usage_line_in_stderr_on_missing_required_operand),
        D_TEST_GENERATE_TEST(test_usage_line_appears_in_help_stdout),
        D_TEST_GENERATE_TEST(test_usage_line_shows_command_placeholder),
        D_TEST_GENERATE_TEST(test_usage_line_shows_operand_names),
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
        /* transform ctx, list/kv init, global lookup */
        D_TEST_GENERATE_TEST(test_transform_ctx_shared_by_two_options),
        D_TEST_GENERATE_TEST(test_transform_null_ctx),
        D_TEST_GENERATE_TEST(test_list_and_kv_init_helpers),
        D_TEST_GENERATE_TEST(test_global_option_after_subcommand_name),
        D_TEST_GENERATE_TEST(test_global_option_not_copied_into_child),
        D_TEST_GENERATE_TEST(test_required_global_checked_when_child_has_no_args),
        D_TEST_GENERATE_TEST(test_child_option_shadows_parent_global),
        D_TEST_GENERATE_TEST(test_non_global_parent_option_unknown_in_child),
        D_TEST_GENERATE_TEST(test_trigger_rules),
        D_TEST_GENERATE_TEST(test_trigger_parses),
        D_TEST_GENERATE_TEST(test_trigger_errors),
        D_TEST_GENERATE_TEST(test_insert_opts_trigger),
    };
    D_TEST_RUN_TESTS(tests);
    return 0;
}
