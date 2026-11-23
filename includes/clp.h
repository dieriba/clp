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
 * OPT_ACT_SET   — stores a single typed value; requires an argument unless TYPE_BOOL.
 * OPT_ACT_COUNT — increments a usize counter each time the flag appears; no argument.
 * OPT_ACT_LIST  — collects comma-separated values into a DDynArray of DStringView.
 * OPT_ACT_KV    — collects key=value pairs (comma-separated) into a DUnorderedMap.
 *
 * OPERAND ACTIONS
 * ---------------
 * OPERAND_ACT_SET  — stores a single typed value.
 * OPERAND_ACT_LIST — consumes all remaining positional arguments into a DDynArray.
 * OPERAND_ACT_KV   — parses a single key=value positional argument into a DUnorderedMap.
 *
 * VALUE TYPES
 * -----------
 * TYPE_BOOL   → value.value_bool         (bool)
 * TYPE_CHAR   → value.value_char         (char)
 * TYPE_STR    → value.value_d_string_view (DStringView; .data is the C string)
 * TYPE_LONG   → value.value_long         (long)
 * TYPE_USIZE  → value.value_usize        (usize)
 * TYPE_DOUBLE → value.value_double       (double)
 *
 * PARSING RULES
 * -------------
 * - "--" terminates option parsing; everything after is treated as an operand.
 * - Options may appear before, between, or after operands (GNU scanning).
 * - "--help" always prints help and exits with 0.
 * - "-h" triggers help only when the user has not registered their own -h option.
 * - "help" is a reserved long option name.
 * - Required operands must be registered before optional ones.
 * - Global options (global=true) are inherited by all descendant commands and are
 *   checked for required status at each level that requires them.
 *
 * OPTION NAME RULES
 * -----------------
 * - Long names must start with a letter and contain only [a-z A-Z 0-9 -].
 * - Short names must be a single alphanumeric character [a-z A-Z 0-9] or '?'.
 * - An option may have only a short name, only a long name, or both.
 *
 * QUICK START
 * -----------
 *   ClpError err;
 *   Command *root;
 *   Option  *verbose;
 *   Operand *file;
 *   if (clp_new_command(&root, 0, "prog", "my program", &err) != CLP_OK ||
 *       clp_new_option(&verbose, "verbose", "v", "enable verbose output", TYPE_BOOL, false, false, &err) != CLP_OK ||
 *       clp_add_command_option(root, verbose, &err) != CLP_OK ||
 *       clp_new_operand(&file, "file", "input file", TYPE_STR, true, &err) != CLP_OK ||
 *       clp_add_command_operand(root, file, &err) != CLP_OK)
 *   {
 *       clp_print_error(stderr, &err);
 *       return 1;
 *   }
 *   Command *cmd = NULL;
 *   clp_parse_args(root, argv, &cmd);
 *   if (verbose->value_set) { ... }
 *   printf("%s\n", file->value.value_d_string_view.data);
 *   clp_cleanup(root);
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
#include "d_unordered_map.h"

#include <stdio.h>

#define clp_new_option(out, long_name, short_name, description, type, required, global, err) clp_new_option_raw(out, long_name, short_name, description, false, OPT_ACT_SET, (Value) {0}, type, required, global, err)

#define clp_new_option_default_long(out, ln, sn, desc, def, req, glob, err) clp_new_option_raw(out, ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_long = (def)}, TYPE_LONG, req, glob, err)

#define clp_new_option_default_bool(out, ln, sn, desc, def, req, glob, err) clp_new_option_raw(out, ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_bool = (def)}, TYPE_BOOL, req, glob, err)

#define clp_new_option_default_usize(out, ln, sn, desc, def, req, glob, err) clp_new_option_raw(out, ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_usize = (def)}, TYPE_USIZE, req, glob, err)

#define clp_new_option_default_str(out, ln, sn, desc, def, req, glob, err) clp_new_option_raw(out, ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_d_string_view = d_string_view_from_c_string(def)}, TYPE_STR, req, glob, err)

#define clp_new_option_default_char(out, ln, sn, desc, def, req, glob, err) clp_new_option_raw(out, ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_char = (def)}, TYPE_CHAR, req, glob, err)

#define clp_new_option_default_double(out, ln, sn, desc, def, req, glob, err) clp_new_option_raw(out, ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_double = (def)}, TYPE_DOUBLE, req, glob, err)

#define clp_new_option_count(out, ln, sn, desc, glob, err) clp_new_option_raw(out, ln, sn, desc, false, OPT_ACT_COUNT, (Value) {0}, TYPE_USIZE, false, glob, err)

#define clp_new_option_list(out, ln, sn, desc, required, glob, err) clp_new_option_raw(out, ln, sn, desc, false, OPT_ACT_LIST, (Value) {0}, TYPE_STR, required, glob, err)

#define clp_new_option_kv(out, ln, sn, desc, required, glob, err) clp_new_option_raw(out, ln, sn, desc, false, OPT_ACT_KV, (Value) {0}, TYPE_STR, required, glob, err)

#define clp_new_operand(out, name, description, type, required, err) clp_new_operand_raw(out, name, description, false, OPERAND_ACT_SET, (Value) {0}, type, required, err)

#define clp_new_operand_default_long(out, name, desc, def, req, err) clp_new_operand_raw(out, name, desc, true, OPERAND_ACT_SET, (Value) {.value_long = (def)}, TYPE_LONG, req, err)

#define clp_new_operand_default_bool(out, name, desc, def, req, err) clp_new_operand_raw(out, name, desc, true, OPERAND_ACT_SET, (Value) {.value_bool = (def)}, TYPE_BOOL, req, err)

#define clp_new_operand_default_usize(out, name, desc, def, req, err) clp_new_operand_raw(out, name, desc, true, OPERAND_ACT_SET, (Value) {.value_usize = (def)}, TYPE_USIZE, req, err)

#define clp_new_operand_default_str(out, name, desc, def, req, err) clp_new_operand_raw(out, name, desc, true, OPERAND_ACT_SET, (Value) {.value_d_string_view = d_string_view_from_c_string(def)}, TYPE_STR, req, err)

#define clp_new_operand_default_char(out, name, desc, def, req, err) clp_new_operand_raw(out, name, desc, true, OPERAND_ACT_SET, (Value) {.value_char = (def)}, TYPE_CHAR, req, err)

#define clp_new_operand_default_double(out, name, desc, def, req, err) clp_new_operand_raw(out, name, desc, true, OPERAND_ACT_SET, (Value) {.value_double = (def)}, TYPE_DOUBLE, req, err)

#define clp_new_operand_list(out, name, desc, required, err) clp_new_operand_raw(out, name, desc, false, OPERAND_ACT_LIST, (Value) {0}, TYPE_STR, required, err)

#define clp_new_operand_kv(out, name, desc, required, err) clp_new_operand_raw(out, name, desc, false, OPERAND_ACT_KV, (Value) {0}, TYPE_STR, required, err)

typedef enum Type
{
    TYPE_LONG,
    TYPE_BOOL,
    TYPE_USIZE,
    TYPE_STR,
    TYPE_CHAR,
    TYPE_DOUBLE,
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

typedef union Value
{
    DUnorderedMap value_kv;
    DDynArray     value_list;
    DStringView   value_d_string_view;
    usize         value_usize;
    double        value_double;
    long          value_long;
    bool          value_bool;
    char          value_char;
} Value;

typedef struct Operand
{
    Value         value;
    DStringView   name;
    char         *description;
    OperandAction action;
    Type          type;
    bool          required;
    bool          has_default_value;
    bool          value_set;
} Operand;

typedef struct Option
{
    Value       value;
    DStringView long_name;
    char       *description;
    OptAction   action;
    Type        type;
    char        short_name;
    bool        required;
    bool        has_default_value;
    bool        value_set;
    bool        global;
    bool        has_args;
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
    CLP_ERR_BAD_VALUE,         /* value could not be converted to the option/operand type */
    CLP_ERR_BAD_KV,            /* malformed key=value pair */
    CLP_ERR_TOO_MANY_OPERANDS, /* more positional arguments than registered operands */
    CLP_ERR_MISSING_REQUIRED,  /* one or more required options/operands were not provided */
    CLP_ERR_DUPLICATE,         /* option registered twice on the same command (same long or short name) */
    CLP_ERR_RESERVED_NAME,     /* option uses a reserved name (e.g. "help") */
    CLP_ERR_INVALID_TYPE,      /* action/type combination not allowed (e.g. count action on a non-usize type) */
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
 *   has_default_value — pass true when value carries a pre-filled default.
 *                       Pass false for LIST/KV so the library allocates the
 *                       internal collection.
 *   action            — OPT_ACT_SET | OPT_ACT_COUNT | OPT_ACT_LIST | OPT_ACT_KV.
 *   value             — initial/default Value; ignored for LIST and KV when
 *                       has_default_value is false.
 *   type              — value type; OPT_ACT_COUNT requires TYPE_USIZE.
 *   required          — if true, clp_parse_args exits when the option is absent.
 *   global            — if true, the option is visible in all descendant commands.
 *
 * out must not be NULL; at least one of long_name or short_name must not be NULL.
 * Returns CLP_OK, CLP_ERR_INVALID_NAME, CLP_ERR_RESERVED_NAME, CLP_ERR_INVALID_TYPE
 * (OPT_ACT_COUNT paired with a type other than TYPE_USIZE), or CLP_ERR_ALLOC.
 */
ClpStatus clp_new_option_raw(Option **out, char *long_name, char *short_name, char *description, bool has_default_value, OptAction action, Value value, Type type, bool required, bool global,
                             ClpError *err);

/* Low-level operand allocator — prefer the clp_new_operand* macros.
 *
 * Allocates a new Operand on the heap and stores it in *out.  The new
 * Operand is owned by the command it is added to; clp_cleanup frees it.
 * Until it is added, it is owned by the caller: free it with clp_free_operand.
 *
 *   name              — operand name shown in usage and --help (e.g. "file").
 *                       Must not be NULL or empty.
 *   description       — help text; NULL is accepted.
 *   has_default_value — pass true when value carries a pre-filled default.
 *                       Pass false for LIST/KV so the library allocates the
 *                       internal collection.
 *   action            — OPERAND_ACT_SET | OPERAND_ACT_LIST | OPERAND_ACT_KV.
 *   value             — initial/default Value; ignored for LIST and KV when
 *                       has_default_value is false.
 *   type              — value type.
 *   required          — if true, clp_parse_args exits when the operand is absent.
 *
 * out and name must not be NULL.
 * Returns CLP_OK, CLP_ERR_INVALID_NAME on an empty or invalid name, or CLP_ERR_ALLOC.
 */
ClpStatus clp_new_operand_raw(Operand **out, char *name, char *description, bool has_default_value, OperandAction action, Value value, Type type, bool required, ClpError *err);

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
 * On failure, options/operands parsed before the error keep the values they
 * received; *command may already point to a dispatched subcommand.
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

/* Release an Option that is not attached to any command, including its
 * internal collection (LIST / KV actions).
 *
 * Use it only for options the caller still owns: one that was never passed to
 * clp_add_command_option, or one whose clp_add_command_option call failed.
 * Never call it on an option that was added to a command — clp_cleanup frees
 * that one, and freeing it here too would be a double free.
 *
 * Passing NULL is a no-op, like free().
 */
void clp_free_option(Option *option);

/* Release an Operand that is not attached to any command, including its
 * internal collection (LIST / KV actions).
 *
 * Use it only for operands the caller still owns: one that was never passed to
 * clp_add_command_operand, or one whose clp_add_command_operand call failed.
 * Never call it on an operand that was added to a command — clp_cleanup frees
 * that one, and freeing it here too would be a double free.
 *
 * Passing NULL is a no-op, like free().
 */
void clp_free_operand(Operand *operand);

/* Recursively release all resources allocated inside the command tree:
 * root, every subcommand, and every option and operand added to them.
 * root must not be NULL.
 */
void clp_cleanup(Command *root);
#endif