/*
 * clp — Command-Line Parser
 * =========================
 *
 * A C library for building command-line interfaces with options, operands,
 * subcommands, and global options.
 *
 * CONCEPTS
 * --------
 * clp_option_t   — a named flag or key/value pair prefixed with - or --.
 *            Short form: -v, -oFILE, -o FILE
 *            Long form:  --verbose, --output=FILE, --output FILE
 *            Combined short: -vvv, -abcVALUE
 *              Each character is processed in order.  Bool/count options
 *              consume their character and continue.  The first value-taking
 *              option consumes the rest of the token as its inline value
 *              (or the next argv token if it is the last character).
 *
 * clp_operand_t  — a positional argument (no leading -).
 *            Positional order matches registration order.
 *
 * clp_command_t  — a named entry point that owns options, operands, or subcommands.
 *            A command cannot have both operands and subcommands.
 *
 * OPTION ACTIONS
 * --------------
 * Every action writes into dest, a variable owned by the caller — except a
 * CLP_TYPE_CUSTOM SET, which hands the argument to a clp_transform_t (see TRANSFORMS).
 *
 * CLP_OPT_ACT_SET   — stores a single typed value in dest; requires an argument
 *                 unless CLP_TYPE_BOOL (a bool flag sets *dest to true).
 * CLP_OPT_ACT_COUNT — increments *dest (usize) each time the flag appears; no argument.
 * CLP_OPT_ACT_TRIGGER — CLP_TYPE_CUSTOM only: calls the transform with arg == NULL
 *                 each time the flag appears; no argument.
 * CLP_OPT_ACT_LIST  — appends comma-separated values to dest, a d_dyn_array_t of d_string_view_t.
 * CLP_OPT_ACT_KV    — inserts key=value pairs (comma-separated) into dest, a
 *                 d_unordered_map_t (d_string_view_t key → d_string_view_t value).
 *
 * OPERAND ACTIONS
 * ---------------
 * CLP_OPERAND_ACT_SET  — stores a single typed value in dest.
 * CLP_OPERAND_ACT_LIST — appends all remaining positional arguments to dest (d_dyn_array_t).
 * CLP_OPERAND_ACT_KV   — inserts one positional argument of comma-separated key=value
 *                    pairs into dest (d_unordered_map_t).
 *
 * LIST and KV collections are owned by the caller.  Create them with
 * clp_list_init / clp_kv_init before parsing (they use the element types the
 * parser expects) and release them with clp_list_destroy / clp_kv_destroy:
 *   d_dyn_array_t     tags;
 *   d_unordered_map_t env;
 *   if (clp_list_init(&tags, &err) != CLP_OK || clp_kv_init(&env, &err) != CLP_OK) ...
 * Stored d_string_view_t values point into argv and stay valid as long as argv does.
 *
 * VALUE TYPES (SET)
 * -----------------
 * CLP_TYPE_BOOL   → bool *
 * CLP_TYPE_CHAR   → char *
 * CLP_TYPE_STR    → d_string_view_t * (.data points into argv, NUL-terminated)
 * CLP_TYPE_LONG   → long *
 * CLP_TYPE_USIZE  → usize *
 * CLP_TYPE_DOUBLE → double *
 * CLP_TYPE_CUSTOM → no dest; the raw argument goes to a clp_transform_t with its ctx
 *
 * The value in dest when parsing ends is the result: what was parsed if the
 * option/operand was given, otherwise whatever the caller put there first
 * (the default).  value_set tells the two apart.  A value rejected by its
 * conversion or checker is never written to dest.
 *
 * TRANSFORMS
 * ----------
 * For arguments the built-in types cannot express (enums, hex patterns, ...),
 * clp_new_option_transform / clp_new_operand_transform take a clp_transform_t
 * and a ctx pointer instead of a dest.  The transform receives the raw
 * argument and ctx, untouched, and does whatever it wants with them — usually
 * parse arg and store the result through ctx.  Passing the target through ctx
 * lets one transform serve several options.  clp only tracks value_set and
 * required.  SET only, no checker.
 *
 * PARSING RULES
 * -------------
 * - "--" terminates option parsing; everything after is treated as an operand.
 * - Options may appear before, between, or after operands (GNU scanning).
 * - "--help" always prints help and exits with 0.
 * - "-h" triggers help only when the user has not registered their own -h option.
 * - "help" is a reserved long option name.
 * - Required operands must be registered before optional ones.
 * - Global options (global=true) are visible in all descendant commands and may
 *   appear before or after the subcommand token.  They are looked up in the
 *   ancestors (nearest first) when the command being parsed has no option of
 *   that name, never copied into it, so a command's own option shadows an
 *   ancestor global of the same name.  Required globals are checked on the
 *   dispatched command, even when it gets no arguments.
 *
 * OPTION NAME RULES
 * -----------------
 * - Long names must start with a letter and contain only [a-z A-Z 0-9 -].
 * - Short names must be a single alphanumeric character [a-z A-Z 0-9] or '?'.
 * - An option may have only a short name, only a long name, or both.
 *
 * QUICK START
 * -----------
 *   bool        verbose = false;          // default value
 *   long        jobs    = 1;              // default value
 *   d_string_view_t file;
 *   clp_error_t    err;
 *   clp_command_t    *root;
 *   clp_option_t     *verbose_opt, *jobs_opt;
 *   clp_operand_t    *file_op;
 *   if (clp_new_command(&root, 0, "prog", "my program", &err) != CLP_OK ||
 *       clp_new_option(&verbose_opt, "verbose", "v", "enable verbose output", &verbose, false, false, &err) != CLP_OK ||
 *       clp_add_command_option(root, verbose_opt, &err) != CLP_OK ||
 *       clp_new_option(&jobs_opt, "jobs", "j", "number of jobs", &jobs, false, false, &err) != CLP_OK ||
 *       clp_add_command_option(root, jobs_opt, &err) != CLP_OK ||
 *       clp_new_operand(&file_op, "file", "input file", &file, true, &err) != CLP_OK ||
 *       clp_add_command_operand(root, file_op, &err) != CLP_OK)
 *   {
 *       // an option/operand whose clp_add_* failed is still yours: clp_free_option / clp_free_operand
 *       clp_print_error(stderr, &err);
 *       return 1;
 *   }
 *   clp_t          clp = {.root = root, .author = "me", .description = "my program", .version = "1.0.0"};
 *   clp_command_t *cmd = NULL;
 *   clp_parse_args(&clp, argv, &cmd);
 *   if (jobs_opt->value_set) { ... }   // given on the command line, not the default
 *   printf("%s %ld %d\n", file.data, jobs, verbose);
 *   clp_cleanup(&clp);
 *
 * A transform option (no dest; the callback stores through its ctx):
 *   Mode mode = MODE_FAST;                 // default value
 *   static const char *parse_mode(void *ctx, const char *arg)
 *   {
 *       Mode *out = ctx;
 *       if (strcmp(arg, "fast") == 0) { *out = MODE_FAST; return NULL; }
 *       if (strcmp(arg, "safe") == 0) { *out = MODE_SAFE; return NULL; }
 *       return "expected \"fast\" or \"safe\"";
 *   }
 *   clp_new_option_transform(&mode_opt, "mode", "m", "run mode", parse_mode, &mode, false, false, &err);
 *
 * ERROR HANDLING
 * --------------
 * Parsing comes in two flavours:
 *   clp_parse_args      — on error prints a message to stderr and calls
 *                         exit(EXIT_FAILURE); "--help" exits with EXIT_SUCCESS.
 *   clp_try_parse_args  — never exits; returns a clp_status_t and fills a clp_error_t
 *                         so the caller decides what to do.
 * Builder functions (clp_new_*, clp_add_*) never exit either: they return a
 * clp_status_t and fill a clp_error_t (see BUILDER ERRORS below).
 * Programming errors (NULL passed to a parameter documented as must not be NULL)
 * are caught by assert and call abort() in debug builds.
 */

#ifndef CLP_H
#define CLP_H

#include "d_dyn_array.h"
#include "d_string_view.h"
#include "d_types.h"
#include <stdio.h>

/* Declared here so KV users need not include "d_unordered_map.h". */
typedef struct d_unordered_map_t d_unordered_map_t;

/* CONSTRUCTOR MACROS
 * ------------------
 * Every value is written straight into a variable the caller owns (dest).
 * clp never assigns a default: initialise dest before parsing and whatever is
 * there when parsing ends is the value — the default if the option/operand was
 * not given.  clp_option_t.value_set / clp_operand_t.value_set tell the two cases apart.
 *
 * For SET the value type is deduced from the dest pointer at compile time:
 *   long *        → CLP_TYPE_LONG       bool *   → CLP_TYPE_BOOL
 *   usize *       → CLP_TYPE_USIZE      char *   → CLP_TYPE_CHAR
 *   d_string_view_t * → CLP_TYPE_STR        double * → CLP_TYPE_DOUBLE
 * Any other pointer type is a compile error.  COUNT takes a usize *, LIST a
 * d_dyn_array_t * and KV a d_unordered_map_t *; anything else is a compile error too.
 * LIST and KV collections must be created by the caller with clp_list_init /
 * clp_kv_init and released with clp_list_destroy / clp_kv_destroy.
 *
 * The _checked variants also take a clp_checker_t (see clp_checker_t).
 *
 * The _transform variants take a clp_transform_t and a ctx instead of a dest:
 * the transform receives the raw argument and ctx (passed through untouched,
 * may be NULL) and handles it however it wants (custom parsing, storing the
 * result through ctx, or nothing at all).  clp only tracks value_set and required.
 * They are SET-only, show <value> in --help and take no checker — the
 * transform does its own validation.
 * clp_new_option_transform_flag makes a transform option that takes no
 * argument (CLP_OPT_ACT_TRIGGER): the transform is called with arg == NULL
 * each time the flag appears, and --help shows no <value>.
 */

/* Map a SET dest pointer to its clp_type_t at compile time; any other pointer type
 * does not compile.  dest is not evaluated here. */
#define CLP_TYPE_OF(dest) _Generic((dest), long *: CLP_TYPE_LONG, bool *: CLP_TYPE_BOOL, usize *: CLP_TYPE_USIZE, d_string_view_t *: CLP_TYPE_STR, char *: CLP_TYPE_CHAR, double *: CLP_TYPE_DOUBLE)

/* Evaluate to dest when it has exactly type T, fail to compile otherwise. */
#define CLP_EXPECT(T, dest) _Generic((dest), T: (dest))

#define clp_new_option(out, ln, sn, desc, dest, required, global, err) clp_new_option_raw(out, ln, sn, desc, dest, CLP_OPT_ACT_SET, CLP_TYPE_OF(dest), required, global, NULL, NULL, err)

#define clp_new_option_count(out, ln, sn, desc, dest, global, err) clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(usize *, dest), CLP_OPT_ACT_COUNT, CLP_TYPE_USIZE, false, global, NULL, NULL, err)

#define clp_new_option_list(out, ln, sn, desc, dest, required, global, err)                                                                                                                            \
    clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(d_dyn_array_t *, dest), CLP_OPT_ACT_LIST, CLP_TYPE_STR, required, global, NULL, NULL, err)

#define clp_new_option_kv(out, ln, sn, desc, dest, required, global, err)                                                                                                                              \
    clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(d_unordered_map_t *, dest), CLP_OPT_ACT_KV, CLP_TYPE_STR, required, global, NULL, NULL, err)

#define clp_new_option_checked(out, ln, sn, desc, dest, required, global, checker, err)                                                                                                                \
    clp_new_option_raw(out, ln, sn, desc, dest, CLP_OPT_ACT_SET, CLP_TYPE_OF(dest), required, global, checker, NULL, err)

#define clp_new_option_list_checked(out, ln, sn, desc, dest, required, global, checker, err)                                                                                                           \
    clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(d_dyn_array_t *, dest), CLP_OPT_ACT_LIST, CLP_TYPE_STR, required, global, checker, NULL, err)

#define clp_new_option_kv_checked(out, ln, sn, desc, dest, required, global, checker, err)                                                                                                             \
    clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(d_unordered_map_t *, dest), CLP_OPT_ACT_KV, CLP_TYPE_STR, required, global, checker, NULL, err)

#define clp_new_operand(out, name, desc, dest, required, err) clp_new_operand_raw(out, name, desc, dest, CLP_OPERAND_ACT_SET, CLP_TYPE_OF(dest), required, NULL, NULL, err)

#define clp_new_operand_list(out, name, desc, dest, required, err)                                                                                                                                     \
    clp_new_operand_raw(out, name, desc, CLP_EXPECT(d_dyn_array_t *, dest), CLP_OPERAND_ACT_LIST, CLP_TYPE_STR, required, NULL, NULL, err)

#define clp_new_operand_kv(out, name, desc, dest, required, err)                                                                                                                                       \
    clp_new_operand_raw(out, name, desc, CLP_EXPECT(d_unordered_map_t *, dest), CLP_OPERAND_ACT_KV, CLP_TYPE_STR, required, NULL, NULL, err)

#define clp_new_operand_checked(out, name, desc, dest, required, checker, err) clp_new_operand_raw(out, name, desc, dest, CLP_OPERAND_ACT_SET, CLP_TYPE_OF(dest), required, checker, NULL, err)

#define clp_new_operand_list_checked(out, name, desc, dest, required, checker, err)                                                                                                                    \
    clp_new_operand_raw(out, name, desc, CLP_EXPECT(d_dyn_array_t *, dest), CLP_OPERAND_ACT_LIST, CLP_TYPE_STR, required, checker, NULL, err)

#define clp_new_operand_kv_checked(out, name, desc, dest, required, checker, err)                                                                                                                      \
    clp_new_operand_raw(out, name, desc, CLP_EXPECT(d_unordered_map_t *, dest), CLP_OPERAND_ACT_KV, CLP_TYPE_STR, required, checker, NULL, err)

#define clp_new_option_transform(out, ln, sn, desc, transform, ctx, required, global, err)                                                                                                             \
    clp_new_option_raw(out, ln, sn, desc, (void *) (ctx), CLP_OPT_ACT_SET, CLP_TYPE_CUSTOM, required, global, NULL, transform, err)

#define clp_new_option_transform_flag(out, ln, sn, desc, transform, ctx, required, global, err)                                                                                                        \
    clp_new_option_raw(out, ln, sn, desc, (void *) (ctx), CLP_OPT_ACT_TRIGGER, CLP_TYPE_CUSTOM, required, global, NULL, transform, err)

#define clp_new_operand_transform(out, name, desc, transform, ctx, required, err)                                                                                                                      \
    clp_new_operand_raw(out, name, desc, (void *) (ctx), CLP_OPERAND_ACT_SET, CLP_TYPE_CUSTOM, required, NULL, transform, err)

typedef enum clp_type_t
{
    CLP_TYPE_LONG,
    CLP_TYPE_BOOL,
    CLP_TYPE_USIZE,
    CLP_TYPE_STR,
    CLP_TYPE_CHAR,
    CLP_TYPE_DOUBLE,
    CLP_TYPE_CUSTOM, /* handled by a clp_transform_t; dest is its ctx, shown as <value> */
} clp_type_t;

typedef enum clp_opt_action_t
{
    CLP_OPT_ACT_COUNT,
    CLP_OPT_ACT_LIST,
    CLP_OPT_ACT_SET,
    CLP_OPT_ACT_KV,
    CLP_OPT_ACT_TRIGGER
} clp_opt_action_t;

typedef enum clp_operand_action_t
{
    CLP_OPERAND_ACT_LIST,
    CLP_OPERAND_ACT_SET,
    CLP_OPERAND_ACT_KV
} clp_operand_action_t;

/* One key=value pair of a KV option/operand, as handed to a clp_checker_t. */
typedef struct clp_key_value_t
{
    d_string_view_t key;
    d_string_view_t value;
} clp_key_value_t;

/* Scratch storage for one SET value while it is converted and checked;
 * it is copied into dest only once accepted. */
typedef union clp_value_t
{
    d_string_view_t value_d_string_view;
    usize           value_usize;
    double          value_double;
    long            value_long;
    bool            value_bool;
    char            value_char;
} clp_value_t;

/* User validator run on a value once it has been converted.
 * value points to:
 *   SET  — the converted value, with the same type as dest (e.g. const long *
 *          for a long dest).  It is not dest itself: dest is only written once
 *          the checker accepts, so a rejected value never overwrites it.
 *   LIST — one item, as a const d_string_view_t *.
 *   KV   — one pair, as a const clp_key_value_t *, after the pair was checked for
 *          a '=' and a non-empty key and value.
 * Never run for bool flags or COUNT.
 * Return NULL to accept, or a message with static lifetime to reject
 * (parse fails with CLP_ERR_BAD_VALUE). */
typedef const char *(*clp_checker_t)(const void *value);

/* User parser for CLP_TYPE_CUSTOM options/operands (the _transform macros).
 * arg is the raw command-line argument, NUL-terminated, pointing into argv.
 * ctx is the pointer given at registration, passed through untouched (it may
 * be NULL); typically where the parsed result goes, so one transform can be
 * shared by several options.
 * For a CLP_OPT_ACT_TRIGGER option (clp_new_option_transform_flag) arg is
 * NULL: the transform runs each time the flag appears.
 * The transform decides what to do with arg: parse it, store the result
 * through ctx (or anywhere else), or ignore it.  clp stores nothing.
 * Return NULL to accept, or a message with static lifetime to reject
 * (parse fails with CLP_ERR_BAD_VALUE and value_set stays false).
 * Anything the transform stored before rejecting is its own responsibility. */
typedef const char *(*clp_transform_t)(void *ctx, const char *arg);

typedef struct clp_operand_t
{
    d_string_view_t      name;
    char                *description;
    void                *dest;      /* caller-owned; for CLP_TYPE_CUSTOM the transform's ctx */
    clp_checker_t        checker;   /* NULL when unchecked */
    clp_transform_t      transform; /* set only for CLP_TYPE_CUSTOM */
    clp_operand_action_t action;
    clp_type_t           type;
    bool                 required;
    bool                 value_set; /* read-only: true once a value was accepted */
} clp_operand_t;

typedef struct clp_option_t
{
    d_string_view_t  long_name;
    char            *description;
    void            *dest;      /* caller-owned; for CLP_TYPE_CUSTOM the transform's ctx */
    clp_checker_t    checker;   /* NULL when unchecked */
    clp_transform_t  transform; /* set only for CLP_TYPE_CUSTOM */
    clp_opt_action_t action;
    clp_type_t       type;
    char             short_name;
    bool             required;
    bool             global;
    bool             has_args;
    bool             value_set; /* read-only: true once the option was given and accepted */
} clp_option_t;

typedef enum clp_status_t
{
    CLP_OK = 0,
    CLP_HELP_SHOWN,            /* help was printed to stdout; not an error, but parsing stopped */
    CLP_ERR_ALLOC,             /* allocation / container failure */
    CLP_ERR_INVALID_NAME,      /* malformed option/operand name, at registration or on the command line (e.g. "--_x") */
    CLP_ERR_UNKNOWN_OPTION,    /* option not registered on the command */
    CLP_ERR_MISSING_ARG,       /* option requires an argument and none was given */
    CLP_ERR_UNEXPECTED_ARG,    /* "--flag=value" on an option that takes no argument */
    CLP_ERR_BAD_VALUE,         /* value could not be converted to its type, or was rejected by a checker or transform */
    CLP_ERR_BAD_KV,            /* malformed key=value pair */
    CLP_ERR_TOO_MANY_OPERANDS, /* more positional arguments than registered operands */
    CLP_ERR_MISSING_REQUIRED,  /* one or more required options/operands were not provided */
    CLP_ERR_DUPLICATE,         /* option registered twice on the same command (same long or short name) */
    CLP_ERR_RESERVED_NAME,     /* option uses a reserved name (e.g. "help") */
    CLP_ERR_INVALID_TYPE,      /* action/type/checker/transform combination not allowed (e.g. count on a non-usize type, transform without CLP_TYPE_CUSTOM) */
    CLP_ERR_CONFLICT,          /* command would have both operands and subcommands */
    CLP_ERR_OPERAND_ORDER,     /* required operand registered after an optional one */
    CLP_ERR_VALIDATOR,         /* rejected by the command's clp_validator_t */
} clp_status_t;

typedef struct clp_error_t clp_error_t;

/* User hook run on a command once its arguments are all parsed and its
 * required arguments are present: the place for rules spanning several
 * options/operands (e.g. "-f and -i are incompatible").
 * Only the validator of the command parse ends on (the one returned through
 * clp_try_parse_args' command) runs; its ancestors' validators do not.
 * ctx is the pointer given to clp_new_command, passed through untouched.
 * Return NULL to accept, or a message with static lifetime to reject (parse
 * fails with CLP_ERR_VALIDATOR, err->command set to the command). */
typedef const char *(*clp_validator_t)(void *ctx);

typedef struct clp_command_t clp_command_t;

struct clp_command_t
{
    d_dyn_array_t   options;
    d_dyn_array_t   operands;
    d_dyn_array_t   sub_commands;
    d_dyn_array_t   extra;
    d_string_view_t name;
    clp_command_t  *parent_command;
    clp_validator_t validator;
    void           *ctx;
    char           *description;
    int             code;
};

/* The program being parsed: the root of its command tree plus program
 * metadata (author, description, version).
 *
 * clp_t owns the tree under root (released by clp_cleanup) but not its
 * strings, which must outlive the parse.  Initialise it directly, e.g.
 *   clp_t clp = {.root = root, .version = "1.0.0"};
 */
typedef struct clp_t
{
    clp_command_t *root;        /* must not be NULL */
    const char    *author;      /* may be NULL */
    const char    *description; /* may be NULL */
    const char    *version;     /* may be NULL */
} clp_t;

#define CLP_ERR_MSG_SIZE 1024

struct clp_error_t
{
    clp_command_t *command;                   /* command being parsed when the error occurred */
    clp_status_t   code;                      /* CLP_OK, CLP_HELP_SHOWN or a CLP_ERR_* code */
    char           message[CLP_ERR_MSG_SIZE]; /* human-readable, may span several lines; truncated if too long */
};

/* Return a short static string naming code (e.g. "unknown option"). */
const char *clp_status_to_str(clp_status_t code);

/* BUILDER ERRORS
 * --------------
 * Every builder function (clp_new_*, clp_add_*) returns a clp_status_t and takes a
 * trailing clp_error_t *err.  On failure the matching CLP_ERR_* code is returned
 * and, when err is not NULL, *err is filled (print it with clp_print_error).
 * err may be NULL.  Constructors write the new object to *out on success and
 * set *out to NULL on failure; out must not be NULL.
 */

/* Allocate and initialise a new clp_command_t on the heap and store it in *out.
 *
 *   code        — user-defined integer identifier; useful for switch dispatch
 *                 after clp_parse_args returns the matched subcommand.
 *   name        — command name as it appears in argv (e.g. "push").
 *                 Must not be NULL.
 *   description — shown in --help output; NULL is accepted (shown as
 *                 "no associated description").
 *   validator   — clp_validator_t run after this command is parsed, or NULL.
 *   ctx         — passed untouched to validator; may be NULL.
 *
 * The returned pointer is owned by the caller.  Set it as a clp_t's root (or
 * add it to a command that is) and pass that clp_t to clp_cleanup to
 * release the command and all its descendant resources.  out and name must not be NULL.
 *
 * Returns CLP_OK, or CLP_ERR_ALLOC.
 */
clp_status_t clp_new_command(clp_command_t **out, int code, char *name, char *description, clp_validator_t validator, void *ctx, clp_error_t *err);

/* Attach sub_command as a child of command.
 *
 * Sets sub_command->parent_command = command.  A command cannot have both
 * operands and subcommands.
 *
 * command and sub_command must not be NULL.
 * Returns CLP_OK, CLP_ERR_CONFLICT if command already has operands, or CLP_ERR_ALLOC.
 * On failure sub_command is not attached and is still owned by the caller.
 */
clp_status_t clp_add_command_sub_command(clp_command_t *command, clp_command_t *sub_command, clp_error_t *err);

/* Register an option on a command.
 *
 * command and command_option must not be NULL.
 * Returns CLP_OK, CLP_ERR_DUPLICATE if the long or short name is already
 * registered on command, or CLP_ERR_ALLOC.
 * On failure command_option is not added and is still owned by the caller;
 * release it with clp_free_option.
 */
clp_status_t clp_add_command_option(clp_command_t *command, clp_option_t *command_option, clp_error_t *err);

/* Everything needed to create one option: the arguments of clp_new_option_raw,
 * as a struct, so a table of them can be handed to clp_insert_opts. */
typedef struct clp_option_info_t
{
    char            *long_name;   /* without "--", or NULL for a short-only option */
    char            *short_name;  /* single character (e.g. "v"), or NULL for a long-only option */
    char            *description; /* help text; NULL is accepted */
    void            *dest;        /* caller-owned; for CLP_TYPE_CUSTOM the transform's ctx */
    clp_opt_action_t action;
    clp_type_t       type;
    bool             required;
    bool             global;
    clp_checker_t    checker;   /* NULL when unchecked */
    clp_transform_t  transform; /* set only for CLP_TYPE_CUSTOM */
} clp_option_info_t;

/* Create count options from infos and register each one on command, in order.
 *
 * Each entry follows the rules of clp_new_option_raw (field for field) and of
 * clp_add_command_option.  Nothing checks that dest matches type.
 *
 * Stops at the first failure: err is filled and its status returned.  The
 * option that failed is released; the ones inserted before it stay on command
 * and are freed by clp_cleanup.
 *
 *   clp_option_info_t opts[] = {
 *       {"verbose", "v", "verbose output", &verbose, CLP_OPT_ACT_SET,   CLP_TYPE_BOOL,  false, false, NULL, NULL},
 *       {"jobs",    "j", "worker count",   &jobs,    CLP_OPT_ACT_SET,   CLP_TYPE_LONG,  false, false, jobs_range, NULL},
 *   };
 *   if (clp_insert_opts(root, opts, sizeof(opts) / sizeof(*opts), &err) != CLP_OK)
 *       ...
 *
 * command must not be NULL, nor infos unless count is 0.
 * Returns CLP_OK, or any status of clp_new_option_raw / clp_add_command_option.
 */
clp_status_t clp_insert_opts(clp_command_t *command, const clp_option_info_t *infos, usize count, clp_error_t *err);

/* Register an operand on a command.
 *
 * Operands are consumed in registration order.  A required operand must not
 * follow an optional one.  A duplicate operand name is accepted (a warning is
 * printed when built with -DWARNING).  A command cannot have both operands and
 * subcommands.
 *
 * command and command_operand must not be NULL.
 * Returns CLP_OK, CLP_ERR_CONFLICT if subcommands are already present,
 * CLP_ERR_OPERAND_ORDER on a required-after-optional ordering violation, or
 * CLP_ERR_ALLOC.  On failure command_operand is not added and is still owned
 * by the caller; release it with clp_free_operand.
 */
clp_status_t clp_add_command_operand(clp_command_t *command, clp_operand_t *command_operand, clp_error_t *err);

/* Everything needed to create one operand: the arguments of
 * clp_new_operand_raw, as a struct, so a table of them can be handed to
 * clp_insert_operands. */
typedef struct clp_operand_info_t
{
    char                *name;        /* shown in usage and --help; not NULL or empty */
    char                *description; /* help text; NULL is accepted */
    void                *dest;        /* caller-owned; for CLP_TYPE_CUSTOM the transform's ctx */
    clp_operand_action_t action;
    clp_type_t           type;
    bool                 required;
    clp_checker_t        checker;   /* NULL when unchecked */
    clp_transform_t      transform; /* set only for CLP_TYPE_CUSTOM */
} clp_operand_info_t;

/* Create count operands from infos and register each one on command, in order.
 *
 * Each entry follows the rules of clp_new_operand_raw (field for field) and of
 * clp_add_command_operand (no operands on a command with subcommands, no
 * required operand after an optional one).  Nothing checks that dest matches
 * type.
 *
 * Stops at the first failure: err is filled and its status returned.  The
 * operand that failed is released; the ones inserted before it stay on
 * command and are freed by clp_cleanup.
 *
 *   clp_operand_info_t ops[] = {
 *       {"host", "host to reach",   &host,  CLP_OPERAND_ACT_SET,  CLP_TYPE_STR, true,  NULL, NULL},
 *       {"args", "extra arguments", &extra, CLP_OPERAND_ACT_LIST, CLP_TYPE_STR, false, NULL, NULL},
 *   };
 *   if (clp_insert_operands(root, ops, sizeof(ops) / sizeof(*ops), &err) != CLP_OK)
 *       ...
 *
 * command must not be NULL, nor infos unless count is 0.
 * Returns CLP_OK, or any status of clp_new_operand_raw / clp_add_command_operand.
 */
clp_status_t clp_insert_operands(clp_command_t *command, const clp_operand_info_t *infos, usize count, clp_error_t *err);

/* Low-level option allocator — prefer the clp_new_option* macros.
 *
 * Allocates a new clp_option_t on the heap and stores it in *out.  The new
 * clp_option_t is owned by the command it is added to; clp_cleanup frees it.
 * Until it is added, it is owned by the caller: free it with clp_free_option.
 *
 *   long_name         — long option name without the "--" prefix, or NULL for
 *                       a short-only option.  Must start with a letter and
 *                       contain only [a-z A-Z 0-9 -].  "help" is reserved.
 *   short_name        — single-character string (e.g. "v"), or NULL for a
 *                       long-only option.  Must be alphanumeric [a-z A-Z 0-9] or '?'.
 *   description       — help text; NULL is accepted.
 *   dest              — caller-owned variable the parsed value is written to;
 *                       its type must match action and type (see VALUE TYPES
 *                       and OPTION ACTIONS).  Its initial content is the
 *                       default.  Must outlive parsing.  For CLP_TYPE_CUSTOM it is
 *                       the transform's ctx instead, and may be NULL.
 *   action            — CLP_OPT_ACT_SET | CLP_OPT_ACT_COUNT | CLP_OPT_ACT_LIST | CLP_OPT_ACT_KV |
 *                       CLP_OPT_ACT_TRIGGER.
 *   type              — value type; CLP_OPT_ACT_COUNT requires CLP_TYPE_USIZE.
 *   required          — if true, clp_parse_args exits when the option is absent.
 *   global            — if true, the option is visible in all descendant commands.
 *   checker           — optional clp_checker_t run on each parsed value (SET) or
 *                       list item (LIST) or key=value pair (KV); never run
 *                       on bool flags or count.
 *                       NULL disables the check.
 *   transform         — clp_transform_t handed the raw argument instead of the
 *                       built-in conversion.  Required with CLP_TYPE_CUSTOM and
 *                       only allowed with it; then action must be CLP_OPT_ACT_SET
 *                       or CLP_OPT_ACT_TRIGGER (no argument, arg == NULL),
 *                       checker must be NULL and dest is passed to it as ctx.
 *                       NULL for every other type.
 *
 * Nothing checks that dest matches type here: prefer the macros, which do.
 * out must not be NULL, nor dest unless type is CLP_TYPE_CUSTOM; at least one of
 * long_name or short_name must not be NULL.
 * Returns CLP_OK, CLP_ERR_INVALID_NAME, CLP_ERR_RESERVED_NAME, CLP_ERR_INVALID_TYPE
 * (CLP_OPT_ACT_COUNT paired with a type other than CLP_TYPE_USIZE,
 * CLP_OPT_ACT_TRIGGER without CLP_TYPE_CUSTOM, or a transform / CLP_TYPE_CUSTOM
 * combination not allowed above), or CLP_ERR_ALLOC.
 */
clp_status_t clp_new_option_raw(clp_option_t **out, char *long_name, char *short_name, char *description, void *dest, clp_opt_action_t action, clp_type_t type, bool required, bool global,
                                clp_checker_t checker, clp_transform_t transform, clp_error_t *err);

/* Low-level operand allocator — prefer the clp_new_operand* macros.
 *
 * Allocates a new clp_operand_t on the heap and stores it in *out.  The new
 * clp_operand_t is owned by the command it is added to; clp_cleanup frees it.
 * Until it is added, it is owned by the caller: free it with clp_free_operand.
 *
 *   name              — operand name shown in usage and --help (e.g. "file").
 *                       Must not be NULL or empty.
 *   description       — help text; NULL is accepted.
 *   dest              — caller-owned variable the parsed value is written to;
 *                       its type must match action and type (see VALUE TYPES
 *                       and OPERAND ACTIONS).  Its initial content is the
 *                       default.  Must outlive parsing.  For CLP_TYPE_CUSTOM it is
 *                       the transform's ctx instead, and may be NULL.
 *   action            — CLP_OPERAND_ACT_SET | CLP_OPERAND_ACT_LIST | CLP_OPERAND_ACT_KV.
 *   type              — value type.
 *   required          — if true, clp_parse_args exits when the operand is absent.
 *   checker           — optional clp_checker_t run on each parsed value (SET) or
 *                       list item (LIST) or key=value pair (KV).
 *                       NULL disables the check.
 *   transform         — same rules as for clp_new_option_raw, with
 *                       CLP_OPERAND_ACT_SET as the only allowed action.
 *
 * Nothing checks that dest matches type here: prefer the macros, which do.
 * out and name must not be NULL, nor dest unless type is CLP_TYPE_CUSTOM.
 * Returns CLP_OK, CLP_ERR_INVALID_NAME on an empty or invalid name,
 * CLP_ERR_INVALID_TYPE on a transform / CLP_TYPE_CUSTOM combination not allowed,
 * or CLP_ERR_ALLOC.
 */
clp_status_t clp_new_operand_raw(clp_operand_t **out, char *name, char *description, void *dest, clp_operand_action_t action, clp_type_t type, bool required, clp_checker_t checker,
                                 clp_transform_t transform, clp_error_t *err);

/* Parse argv against the command tree rooted at clp->root.
 *
 * argv must be the raw argv from main (argv[0] is the program name).
 * On return, *command points to the deepest dispatched subcommand, or stays
 * NULL if no subcommand token was matched.
 *
 * Parsing follows GNU conventions: options may appear before, between, or
 * after operands.  "--" terminates option scanning; everything after it is
 * treated as an operand.  "--help" (and "-h" when the user has not registered
 * their own -h) prints help and exits with EXIT_SUCCESS.
 *
 * Global options marked on a parent are collected into the matched subcommand
 * after parsing and checked for required status.
 *
 * clp, clp->root, argv, *argv, and command must not be NULL.
 * Exits on unknown options, type conversion failures, too many operands, or any
 * missing required option/operand; prints usage and a "--help" hint before exiting.
 */
void clp_parse_args(clp_t *clp, char **argv, clp_command_t **command);

/* Same as clp_parse_args, but never exits.
 *
 * Returns CLP_OK on success.  On "--help" (or "-h", see clp_parse_args) the
 * help text is printed to stdout and CLP_HELP_SHOWN is returned.  On any other
 * failure the matching CLP_ERR_* code is returned and, when err is not NULL,
 * *err is filled with the code, command and a formatted message (print it with
 * clp_print_error).  err may be NULL if the caller only needs the status.
 *
 * On failure, dests written before the error keep the values they received
 * (for LIST/KV, so do the items accepted before the bad one) and transforms
 * that already accepted an argument are not undone; the rejected value itself
 * is never written.  *command may already point to a dispatched subcommand.
 *
 * clp, clp->root, argv, *argv, and command must not be NULL.
 */
clp_status_t clp_try_parse_args(clp_t *clp, char **argv, clp_command_t **command, clp_error_t *err);

/* Print the help for root to stdout: the usage line, description, options,
 * subcommands and operands, in the same format "--help" uses.
 * root must not be NULL.
 */
void clp_print_help(clp_command_t *root);

/* Print err in the same format clp_parse_args uses before exiting
 * ("clp: <message>", plus the usage line for CLP_ERR_MISSING_REQUIRED).
 * Prints nothing for CLP_OK and CLP_HELP_SHOWN.  stream and err must not be NULL.
 */
void clp_print_error(FILE *stream, const clp_error_t *err);

/* Return the option registered on command with the given short name, or NULL.
 * command must not be NULL.
 */
clp_option_t *clp_get_option_by_short(clp_command_t *command, char shrt);

/* Return the option registered on command with the given long name, or NULL.
 * command must not be NULL and lng must not be an empty view.
 */
clp_option_t *clp_get_option_by_long(clp_command_t *command, d_string_view_t lng);

/* Return the operand registered on command with the given name, or NULL.
 * command must not be NULL.
 */
clp_operand_t *clp_get_operand(clp_command_t *command, d_string_view_t operand_name);

/* Release an clp_option_t that is not attached to any command.  Its dest is
 * the caller's and is left untouched.
 *
 * Use it only for options the caller still owns: one that was never passed to
 * clp_add_command_option, or one whose clp_add_command_option call failed.
 * Never call it on an option that was added to a command — clp_cleanup frees
 * that one, and freeing it here too would be a double free.
 *
 * Passing NULL is a no-op, like free().
 */
void clp_free_option(clp_option_t *option);

/* Release an clp_operand_t that is not attached to any command.  Its dest is
 * the caller's and is left untouched.
 *
 * Use it only for operands the caller still owns: one that was never passed to
 * clp_add_command_operand, or one whose clp_add_command_operand call failed.
 * Never call it on an operand that was added to a command — clp_cleanup frees
 * that one, and freeing it here too would be a double free.
 *
 * Passing NULL is a no-op, like free().
 */
void clp_free_operand(clp_operand_t *operand);

/* Create an empty LIST collection with the element type the parser expects
 * (d_string_view_t).  Release it with clp_list_destroy.  list must not be NULL.
 * Returns CLP_OK or CLP_ERR_ALLOC.
 */
clp_status_t clp_list_init(d_dyn_array_t *list, clp_error_t *err);

/* Create an empty KV collection with the key/value types the parser expects
 * (d_string_view_t → d_string_view_t, keys not owned).  Release it with
 * clp_kv_destroy.  map must not be NULL.  Returns CLP_OK or CLP_ERR_ALLOC.
 */
clp_status_t clp_kv_init(d_unordered_map_t *map, clp_error_t *err);

/* Release a collection created by clp_list_init / clp_kv_init.  The
 * d_string_view_t values it held point into argv, which is not touched. */
void clp_list_destroy(d_dyn_array_t *list);
void clp_kv_destroy(d_unordered_map_t *map);

/* Recursively release all resources allocated inside the command tree of
 * clp: clp->root, every subcommand, and every option and operand added to
 * them, then set clp->root to NULL.  The clp_t itself and its strings are not
 * freed.  Passing NULL (or a clp_t whose root is NULL) is a no-op, like free().
 */
void clp_cleanup(clp_t *clp);
#endif