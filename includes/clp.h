/*
 * clp — Command-Line Parser
 * =========================
 *
 * A C library for building command-line interfaces with options, operands,
 * subcommands, and global options.
 *
 * CONCEPTS
 * --------
 * Option   — a named flag or key/value pair prefixed with - or --.
 *            Short form: -v, -oFILE, -o FILE
 *            Long form:  --verbose, --output=FILE, --output FILE
 *            Combined short: -vvv, -abcVALUE
 *              Each character is processed in order.  Bool/count options
 *              consume their character and continue.  The first value-taking
 *              option consumes the rest of the token as its inline value
 *              (or the next argv token if it is the last character).
 *
 * Operand  — a positional argument (no leading -).
 *            Positional order matches registration order.
 *
 * Command  — a named entry point that owns options, operands, or subcommands.
 *            A command cannot have both operands and subcommands.
 *
 * OPTION ACTIONS
 * --------------
 * Every action writes into dest, a variable owned by the caller — except a
 * TYPE_CUSTOM SET, which hands the argument to a ClpTransform (see TRANSFORMS).
 *
 * OPT_ACT_SET   — stores a single typed value in dest; requires an argument
 *                 unless TYPE_BOOL (a bool flag sets *dest to true).
 * OPT_ACT_COUNT — increments *dest (usize) each time the flag appears; no argument.
 * OPT_ACT_LIST  — appends comma-separated values to dest, a DDynArray of DStringView.
 * OPT_ACT_KV    — inserts key=value pairs (comma-separated) into dest, a
 *                 DUnorderedMap (DStringView key → DStringView value).
 *
 * OPERAND ACTIONS
 * ---------------
 * OPERAND_ACT_SET  — stores a single typed value in dest.
 * OPERAND_ACT_LIST — appends all remaining positional arguments to dest (DDynArray).
 * OPERAND_ACT_KV   — inserts one positional argument of comma-separated key=value
 *                    pairs into dest (DUnorderedMap).
 *
 * LIST and KV collections are owned by the caller.  Create them with
 * clp_list_init / clp_kv_init before parsing (they use the element types the
 * parser expects) and release them with clp_list_destroy / clp_kv_destroy:
 *   DDynArray     tags;
 *   DUnorderedMap env;
 *   if (clp_list_init(&tags, &err) != CLP_OK || clp_kv_init(&env, &err) != CLP_OK) ...
 * Stored DStringViews point into argv and stay valid as long as argv does.
 *
 * VALUE TYPES (SET)
 * -----------------
 * TYPE_BOOL   → bool *
 * TYPE_CHAR   → char *
 * TYPE_STR    → DStringView * (.data points into argv, NUL-terminated)
 * TYPE_LONG   → long *
 * TYPE_USIZE  → usize *
 * TYPE_DOUBLE → double *
 * TYPE_CUSTOM → no dest; the raw argument goes to a ClpTransform with its ctx
 *
 * The value in dest when parsing ends is the result: what was parsed if the
 * option/operand was given, otherwise whatever the caller put there first
 * (the default).  value_set tells the two apart.  A value rejected by its
 * conversion or checker is never written to dest.
 *
 * TRANSFORMS
 * ----------
 * For arguments the built-in types cannot express (enums, hex patterns, ...),
 * clp_new_option_transform / clp_new_operand_transform take a ClpTransform
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
 *   DStringView file;
 *   ClpError    err;
 *   Command    *root;
 *   Option     *verbose_opt, *jobs_opt;
 *   Operand    *file_op;
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
 *   Command *cmd = NULL;
 *   clp_parse_args(root, argv, &cmd);
 *   if (jobs_opt->value_set) { ... }   // given on the command line, not the default
 *   printf("%s %ld %d\n", file.data, jobs, verbose);
 *   clp_cleanup(root);
 *
 * A transform option (no dest; the callback stores through its ctx):
 *   Mode mode = MODE_FAST;                 // default value
 *   static const char *parse_mode(const char *arg, void *ctx)
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
 *   clp_try_parse_args  — never exits; returns a ClpStatus and fills a ClpError
 *                         so the caller decides what to do.
 * Builder functions (clp_new_*, clp_add_*) never exit either: they return a
 * ClpStatus and fill a ClpError (see BUILDER ERRORS below).
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
typedef struct DUnorderedMap DUnorderedMap;

/* CONSTRUCTOR MACROS
 * ------------------
 * Every value is written straight into a variable the caller owns (dest).
 * clp never assigns a default: initialise dest before parsing and whatever is
 * there when parsing ends is the value — the default if the option/operand was
 * not given.  Option.value_set / Operand.value_set tell the two cases apart.
 *
 * For SET the value type is deduced from the dest pointer at compile time:
 *   long *        → TYPE_LONG       bool *   → TYPE_BOOL
 *   usize *       → TYPE_USIZE      char *   → TYPE_CHAR
 *   DStringView * → TYPE_STR        double * → TYPE_DOUBLE
 * Any other pointer type is a compile error.  COUNT takes a usize *, LIST a
 * DDynArray * and KV a DUnorderedMap *; anything else is a compile error too.
 * LIST and KV collections must be created by the caller with clp_list_init /
 * clp_kv_init and released with clp_list_destroy / clp_kv_destroy.
 *
 * The _checked variants also take a ClpChecker (see ClpChecker).
 *
 * The _transform variants take a ClpTransform and a ctx instead of a dest:
 * the transform receives the raw argument and ctx (passed through untouched,
 * may be NULL) and handles it however it wants (custom parsing, storing the
 * result through ctx, or nothing at all).  clp only tracks value_set and required.
 * They are SET-only, show <value> in --help and take no checker — the
 * transform does its own validation.
 */

/* Map a SET dest pointer to its Type at compile time; any other pointer type
 * does not compile.  dest is not evaluated here. */
#define CLP_TYPE_OF(dest) _Generic((dest), long *: TYPE_LONG, bool *: TYPE_BOOL, usize *: TYPE_USIZE, DStringView *: TYPE_STR, char *: TYPE_CHAR, double *: TYPE_DOUBLE)

/* Evaluate to dest when it has exactly type T, fail to compile otherwise. */
#define CLP_EXPECT(T, dest) _Generic((dest), T: (dest))

#define clp_new_option(out, ln, sn, desc, dest, required, global, err) clp_new_option_raw(out, ln, sn, desc, dest, OPT_ACT_SET, CLP_TYPE_OF(dest), required, global, NULL, NULL, err)

#define clp_new_option_count(out, ln, sn, desc, dest, global, err) clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(usize *, dest), OPT_ACT_COUNT, TYPE_USIZE, false, global, NULL, NULL, err)

#define clp_new_option_list(out, ln, sn, desc, dest, required, global, err) clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(DDynArray *, dest), OPT_ACT_LIST, TYPE_STR, required, global, NULL, NULL, err)

#define clp_new_option_kv(out, ln, sn, desc, dest, required, global, err) clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(DUnorderedMap *, dest), OPT_ACT_KV, TYPE_STR, required, global, NULL, NULL, err)

#define clp_new_option_checked(out, ln, sn, desc, dest, required, global, checker, err) clp_new_option_raw(out, ln, sn, desc, dest, OPT_ACT_SET, CLP_TYPE_OF(dest), required, global, checker, NULL, err)

#define clp_new_option_list_checked(out, ln, sn, desc, dest, required, global, checker, err)                                                                                                           \
    clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(DDynArray *, dest), OPT_ACT_LIST, TYPE_STR, required, global, checker, NULL, err)

#define clp_new_option_kv_checked(out, ln, sn, desc, dest, required, global, checker, err)                                                                                                             \
    clp_new_option_raw(out, ln, sn, desc, CLP_EXPECT(DUnorderedMap *, dest), OPT_ACT_KV, TYPE_STR, required, global, checker, NULL, err)

#define clp_new_operand(out, name, desc, dest, required, err) clp_new_operand_raw(out, name, desc, dest, OPERAND_ACT_SET, CLP_TYPE_OF(dest), required, NULL, NULL, err)

#define clp_new_operand_list(out, name, desc, dest, required, err) clp_new_operand_raw(out, name, desc, CLP_EXPECT(DDynArray *, dest), OPERAND_ACT_LIST, TYPE_STR, required, NULL, NULL, err)

#define clp_new_operand_kv(out, name, desc, dest, required, err) clp_new_operand_raw(out, name, desc, CLP_EXPECT(DUnorderedMap *, dest), OPERAND_ACT_KV, TYPE_STR, required, NULL, NULL, err)

#define clp_new_operand_checked(out, name, desc, dest, required, checker, err) clp_new_operand_raw(out, name, desc, dest, OPERAND_ACT_SET, CLP_TYPE_OF(dest), required, checker, NULL, err)

#define clp_new_operand_list_checked(out, name, desc, dest, required, checker, err)                                                                                                                    \
    clp_new_operand_raw(out, name, desc, CLP_EXPECT(DDynArray *, dest), OPERAND_ACT_LIST, TYPE_STR, required, checker, NULL, err)

#define clp_new_operand_kv_checked(out, name, desc, dest, required, checker, err)                                                                                                                      \
    clp_new_operand_raw(out, name, desc, CLP_EXPECT(DUnorderedMap *, dest), OPERAND_ACT_KV, TYPE_STR, required, checker, NULL, err)

#define clp_new_option_transform(out, ln, sn, desc, transform, ctx, required, global, err)                                                                                                            \
    clp_new_option_raw(out, ln, sn, desc, (void *) (ctx), OPT_ACT_SET, TYPE_CUSTOM, required, global, NULL, transform, err)

#define clp_new_operand_transform(out, name, desc, transform, ctx, required, err)                                                                                                                     \
    clp_new_operand_raw(out, name, desc, (void *) (ctx), OPERAND_ACT_SET, TYPE_CUSTOM, required, NULL, transform, err)

typedef enum Type
{
    TYPE_LONG,
    TYPE_BOOL,
    TYPE_USIZE,
    TYPE_STR,
    TYPE_CHAR,
    TYPE_DOUBLE,
    TYPE_CUSTOM, /* handled by a ClpTransform; dest is its ctx, shown as <value> */
} Type;

typedef enum OptAction
{
    OPT_ACT_COUNT,
    OPT_ACT_LIST,
    OPT_ACT_SET,
    OPT_ACT_KV
} OptAction;

typedef enum OperandAction
{
    OPERAND_ACT_LIST,
    OPERAND_ACT_SET,
    OPERAND_ACT_KV
} OperandAction;

/* One key=value pair of a KV option/operand, as handed to a ClpChecker. */
typedef struct ClpKeyValue
{
    DStringView key;
    DStringView value;
} ClpKeyValue;

/* Scratch storage for one SET value while it is converted and checked;
 * it is copied into dest only once accepted. */
typedef union Value
{
    DStringView value_d_string_view;
    usize       value_usize;
    double      value_double;
    long        value_long;
    bool        value_bool;
    char        value_char;
} Value;

/* User validator run on a value once it has been converted.
 * value points to:
 *   SET  — the converted value, with the same type as dest (e.g. const long *
 *          for a long dest).  It is not dest itself: dest is only written once
 *          the checker accepts, so a rejected value never overwrites it.
 *   LIST — one item, as a const DStringView *.
 *   KV   — one pair, as a const ClpKeyValue *, after the pair was checked for
 *          a '=' and a non-empty key and value.
 * Never run for bool flags or COUNT.
 * Return NULL to accept, or a message with static lifetime to reject
 * (parse fails with CLP_ERR_BAD_VALUE). */
typedef const char *(*ClpChecker)(const void *value);

/* User parser for TYPE_CUSTOM options/operands (the _transform macros).
 * arg is the raw command-line argument, NUL-terminated, pointing into argv.
 * ctx is the pointer given at registration, passed through untouched (it may
 * be NULL); typically where the parsed result goes, so one transform can be
 * shared by several options.
 * The transform decides what to do with arg: parse it, store the result
 * through ctx (or anywhere else), or ignore it.  clp stores nothing.
 * Return NULL to accept, or a message with static lifetime to reject
 * (parse fails with CLP_ERR_BAD_VALUE and value_set stays false).
 * Anything the transform stored before rejecting is its own responsibility. */
typedef const char *(*ClpTransform)(const char *arg, void *ctx);

typedef struct Operand
{
    DStringView   name;
    char         *description;
    void         *dest;      /* caller-owned; for TYPE_CUSTOM the transform's ctx */
    ClpChecker    checker;   /* NULL when unchecked */
    ClpTransform  transform; /* set only for TYPE_CUSTOM */
    OperandAction action;
    Type          type;
    bool          required;
    bool          value_set; /* read-only: true once a value was accepted */
} Operand;

typedef struct Option
{
    DStringView  long_name;
    char        *description;
    void        *dest;      /* caller-owned; for TYPE_CUSTOM the transform's ctx */
    ClpChecker   checker;   /* NULL when unchecked */
    ClpTransform transform; /* set only for TYPE_CUSTOM */
    OptAction    action;
    Type         type;
    char         short_name;
    bool         required;
    bool         global;
    bool         has_args;
    bool         value_set; /* read-only: true once the option was given and accepted */
} Option;

typedef enum ClpStatus
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
    CLP_ERR_INVALID_TYPE,      /* action/type/checker/transform combination not allowed (e.g. count on a non-usize type, transform without TYPE_CUSTOM) */
    CLP_ERR_CONFLICT,          /* command would have both operands and subcommands */
    CLP_ERR_OPERAND_ORDER,     /* required operand registered after an optional one */
} ClpStatus;

typedef struct Command Command;

struct Command
{
    DDynArray   options;
    DDynArray   operands;
    DDynArray   sub_commands;
    DDynArray   extra;
    DStringView name;
    Command    *parent_command;
    char       *description;
    int         code;
};

#define CLP_ERR_MSG_SIZE 1024

typedef struct ClpError
{
    Command  *command;                   /* command being parsed when the error occurred */
    ClpStatus code;                      /* CLP_OK, CLP_HELP_SHOWN or a CLP_ERR_* code */
    char      message[CLP_ERR_MSG_SIZE]; /* human-readable, may span several lines; truncated if too long */
} ClpError;

/* Return a short static string naming code (e.g. "unknown option"). */
const char *clp_status_to_str(ClpStatus code);

/* BUILDER ERRORS
 * --------------
 * Every builder function (clp_new_*, clp_add_*) returns a ClpStatus and takes a
 * trailing ClpError *err.  On failure the matching CLP_ERR_* code is returned
 * and, when err is not NULL, *err is filled (print it with clp_print_error).
 * err may be NULL.  Constructors write the new object to *out on success and
 * set *out to NULL on failure; out must not be NULL.
 */

/* Allocate and initialise a new Command on the heap and store it in *out.
 *
 *   code        — user-defined integer identifier; useful for switch dispatch
 *                 after clp_parse_args returns the matched subcommand.
 *   name        — command name as it appears in argv (e.g. "push").
 *                 Must not be NULL.
 *   description — shown in --help output; NULL is accepted (shown as
 *                 "no associated description").
 *
 * The returned pointer is owned by the caller.  Pass it to clp_cleanup to
 * release the command and all its descendant resources.  out and name must not be NULL.
 *
 * Returns CLP_OK, or CLP_ERR_ALLOC.
 */
ClpStatus clp_new_command(Command **out, int code, char *name, char *description, ClpError *err);

/* Attach sub_command as a child of command.
 *
 * Sets sub_command->parent_command = command.  A command cannot have both
 * operands and subcommands.
 *
 * command and sub_command must not be NULL.
 * Returns CLP_OK, CLP_ERR_CONFLICT if command already has operands, or CLP_ERR_ALLOC.
 * On failure sub_command is not attached and is still owned by the caller.
 */
ClpStatus clp_add_command_sub_command(Command *command, Command *sub_command, ClpError *err);

/* Register an option on a command.
 *
 * command and command_option must not be NULL.
 * Returns CLP_OK, CLP_ERR_DUPLICATE if the long or short name is already
 * registered on command, or CLP_ERR_ALLOC.
 * On failure command_option is not added and is still owned by the caller;
 * release it with clp_free_option.
 */
ClpStatus clp_add_command_option(Command *command, Option *command_option, ClpError *err);

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
ClpStatus clp_add_command_operand(Command *command, Operand *command_operand, ClpError *err);

/* Low-level option allocator — prefer the clp_new_option* macros.
 *
 * Allocates a new Option on the heap and stores it in *out.  The new
 * Option is owned by the command it is added to; clp_cleanup frees it.
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
 *                       default.  Must outlive parsing.  For TYPE_CUSTOM it is
 *                       the transform's ctx instead, and may be NULL.
 *   action            — OPT_ACT_SET | OPT_ACT_COUNT | OPT_ACT_LIST | OPT_ACT_KV.
 *   type              — value type; OPT_ACT_COUNT requires TYPE_USIZE.
 *   required          — if true, clp_parse_args exits when the option is absent.
 *   global            — if true, the option is visible in all descendant commands.
 *   checker           — optional ClpChecker run on each parsed value (SET) or
 *                       list item (LIST) or key=value pair (KV); never run
 *                       on bool flags or count.
 *                       NULL disables the check.
 *   transform         — ClpTransform handed the raw argument instead of the
 *                       built-in conversion.  Required with TYPE_CUSTOM and
 *                       only allowed with it; then action must be OPT_ACT_SET,
 *                       checker must be NULL and dest is passed to it as ctx.
 *                       NULL for every other type.
 *
 * Nothing checks that dest matches type here: prefer the macros, which do.
 * out must not be NULL, nor dest unless type is TYPE_CUSTOM; at least one of
 * long_name or short_name must not be NULL.
 * Returns CLP_OK, CLP_ERR_INVALID_NAME, CLP_ERR_RESERVED_NAME, CLP_ERR_INVALID_TYPE
 * (OPT_ACT_COUNT paired with a type other than TYPE_USIZE, or a transform /
 * TYPE_CUSTOM combination not allowed above), or CLP_ERR_ALLOC.
 */
ClpStatus clp_new_option_raw(Option **out, char *long_name, char *short_name, char *description, void *dest, OptAction action, Type type, bool required, bool global, ClpChecker checker,
                             ClpTransform transform, ClpError *err);

/* Low-level operand allocator — prefer the clp_new_operand* macros.
 *
 * Allocates a new Operand on the heap and stores it in *out.  The new
 * Operand is owned by the command it is added to; clp_cleanup frees it.
 * Until it is added, it is owned by the caller: free it with clp_free_operand.
 *
 *   name              — operand name shown in usage and --help (e.g. "file").
 *                       Must not be NULL or empty.
 *   description       — help text; NULL is accepted.
 *   dest              — caller-owned variable the parsed value is written to;
 *                       its type must match action and type (see VALUE TYPES
 *                       and OPERAND ACTIONS).  Its initial content is the
 *                       default.  Must outlive parsing.  For TYPE_CUSTOM it is
 *                       the transform's ctx instead, and may be NULL.
 *   action            — OPERAND_ACT_SET | OPERAND_ACT_LIST | OPERAND_ACT_KV.
 *   type              — value type.
 *   required          — if true, clp_parse_args exits when the operand is absent.
 *   checker           — optional ClpChecker run on each parsed value (SET) or
 *                       list item (LIST) or key=value pair (KV).
 *                       NULL disables the check.
 *   transform         — same rules as for clp_new_option_raw, with
 *                       OPERAND_ACT_SET as the only allowed action.
 *
 * Nothing checks that dest matches type here: prefer the macros, which do.
 * out and name must not be NULL, nor dest unless type is TYPE_CUSTOM.
 * Returns CLP_OK, CLP_ERR_INVALID_NAME on an empty or invalid name,
 * CLP_ERR_INVALID_TYPE on a transform / TYPE_CUSTOM combination not allowed,
 * or CLP_ERR_ALLOC.
 */
ClpStatus clp_new_operand_raw(Operand **out, char *name, char *description, void *dest, OperandAction action, Type type, bool required, ClpChecker checker, ClpTransform transform,
                              ClpError *err);

/* Parse argv against the command tree rooted at root.
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
 * root, argv, *argv, and command must not be NULL.
 * Exits on unknown options, type conversion failures, too many operands, or any
 * missing required option/operand; prints usage and a "--help" hint before exiting.
 */
void clp_parse_args(Command *root, char **argv, Command **command);

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
 * root, argv, *argv, and command must not be NULL.
 */
ClpStatus clp_try_parse_args(Command *root, char **argv, Command **command, ClpError *err);

/* Print err in the same format clp_parse_args uses before exiting
 * ("clp: <message>", plus the usage line for CLP_ERR_MISSING_REQUIRED).
 * Prints nothing for CLP_OK and CLP_HELP_SHOWN.  stream and err must not be NULL.
 */
void clp_print_error(FILE *stream, const ClpError *err);

/* Return the option registered on command with the given short name, or NULL.
 * command must not be NULL.
 */
Option *clp_get_option_by_short(Command *command, char shrt);

/* Return the option registered on command with the given long name, or NULL.
 * command must not be NULL and lng must not be an empty view.
 */
Option *clp_get_option_by_long(Command *command, DStringView lng);

/* Return the operand registered on command with the given name, or NULL.
 * command must not be NULL.
 */
Operand *clp_get_operand(Command *command, DStringView operand_name);

/* Release an Option that is not attached to any command.  Its dest is
 * the caller's and is left untouched.
 *
 * Use it only for options the caller still owns: one that was never passed to
 * clp_add_command_option, or one whose clp_add_command_option call failed.
 * Never call it on an option that was added to a command — clp_cleanup frees
 * that one, and freeing it here too would be a double free.
 *
 * Passing NULL is a no-op, like free().
 */
void clp_free_option(Option *option);

/* Release an Operand that is not attached to any command.  Its dest is
 * the caller's and is left untouched.
 *
 * Use it only for operands the caller still owns: one that was never passed to
 * clp_add_command_operand, or one whose clp_add_command_operand call failed.
 * Never call it on an operand that was added to a command — clp_cleanup frees
 * that one, and freeing it here too would be a double free.
 *
 * Passing NULL is a no-op, like free().
 */
void clp_free_operand(Operand *operand);

/* Create an empty LIST collection with the element type the parser expects
 * (DStringView).  Release it with clp_list_destroy.  list must not be NULL.
 * Returns CLP_OK or CLP_ERR_ALLOC.
 */
ClpStatus clp_list_init(DDynArray *list, ClpError *err);

/* Create an empty KV collection with the key/value types the parser expects
 * (DStringView → DStringView, keys not owned).  Release it with
 * clp_kv_destroy.  map must not be NULL.  Returns CLP_OK or CLP_ERR_ALLOC.
 */
ClpStatus clp_kv_init(DUnorderedMap *map, ClpError *err);

/* Release a collection created by clp_list_init / clp_kv_init.  The
 * DStringViews it held point into argv, which is not touched. */
void clp_list_destroy(DDynArray *list);
void clp_kv_destroy(DUnorderedMap *map);

/* Recursively release all resources allocated inside the command tree:
 * root, every subcommand, and every option and operand added to them.
 * root must not be NULL.
 */
void clp_cleanup(Command *root);
#endif