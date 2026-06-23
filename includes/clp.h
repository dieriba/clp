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
 * - Short names must be a single alphanumeric character [a-z A-Z 0-9].
 * - An option may have only a short name, only a long name, or both.
 *
 * QUICK START
 * -----------
 *   Command *root    = clp_new_command(0, "prog", "my program");
 *   Option  *verbose = clp_new_option("verbose", "v", "enable verbose output", TYPE_BOOL, false, false);
 *   Operand *file    = clp_new_operand("file", "input file", TYPE_STR, true);
 *   clp_add_command_option(root, verbose);
 *   clp_add_command_operand(root, file);
 *   Command *cmd = NULL;
 *   clp_parse_args(root, argv, &cmd);
 *   if (verbose->value_set) { ... }
 *   printf("%s\n", file->value.value_d_string_view.data);
 *   clp_cleanup(root);
 *
 * ERROR HANDLING
 * --------------
 * Runtime errors (bad option names, duplicates, OOM, parse failures) print a
 * message to stderr and call exit(EXIT_FAILURE).  There is no return-code error path.
 * Programming errors (NULL passed to a parameter documented as must not be NULL)
 * are caught by assert and call abort() in debug builds.
 */

#ifndef CLP_H
#define CLP_H

#include "d_dyn_array.h"
#include "d_string_view.h"
#include "d_types.h"
#include "d_unordered_map.h"

#define clp_new_option(long_name, short_name, description, type, required, global) clp_new_option_raw(long_name, short_name, description, false, OPT_ACT_SET, (Value) {0}, type, required, global)

#define clp_new_option_default_long(ln, sn, desc, def, req, glob) clp_new_option_raw(ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_long = (def)}, TYPE_LONG, req, glob)

#define clp_new_option_default_bool(ln, sn, desc, def, req, glob) clp_new_option_raw(ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_bool = (def)}, TYPE_BOOL, req, glob)

#define clp_new_option_default_usize(ln, sn, desc, def, req, glob) clp_new_option_raw(ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_usize = (def)}, TYPE_USIZE, req, glob)

#define clp_new_option_default_str(ln, sn, desc, def, req, glob)                                                                                                                                       \
    clp_new_option_raw(ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_d_string_view = d_string_view_from_c_string(def)}, TYPE_STR, req, glob)

#define clp_new_option_default_char(ln, sn, desc, def, req, glob) clp_new_option_raw(ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_char = (def)}, TYPE_CHAR, req, glob)

#define clp_new_option_default_double(ln, sn, desc, def, req, glob) clp_new_option_raw(ln, sn, desc, true, OPT_ACT_SET, (Value) {.value_double = (def)}, TYPE_DOUBLE, req, glob)

#define clp_new_option_count(ln, sn, desc, glob) clp_new_option_raw(ln, sn, desc, false, OPT_ACT_COUNT, (Value) {0}, TYPE_USIZE, false, glob)

#define clp_new_option_list(ln, sn, desc, required, glob) clp_new_option_raw(ln, sn, desc, false, OPT_ACT_LIST, (Value) {0}, TYPE_STR, required, glob)

#define clp_new_option_kv(ln, sn, desc, required, glob) clp_new_option_raw(ln, sn, desc, false, OPT_ACT_KV, (Value) {0}, TYPE_STR, required, glob)

#define clp_new_operand(name, description, type, required) clp_new_operand_raw(name, description, false, OPERAND_ACT_SET, (Value) {0}, type, required)

#define clp_new_operand_default_long(name, desc, def, req) clp_new_operand_raw(name, desc, true, OPERAND_ACT_SET, (Value) {.value_long = (def)}, TYPE_LONG, req)

#define clp_new_operand_default_bool(name, desc, def, req) clp_new_operand_raw(name, desc, true, OPERAND_ACT_SET, (Value) {.value_bool = (def)}, TYPE_BOOL, req)

#define clp_new_operand_default_usize(name, desc, def, req) clp_new_operand_raw(name, desc, true, OPERAND_ACT_SET, (Value) {.value_usize = (def)}, TYPE_USIZE, req)

#define clp_new_operand_default_str(name, desc, def, req)                                                                                                                                              \
    clp_new_operand_raw(name, desc, true, OPERAND_ACT_SET, (Value) {.value_d_string_view = d_string_view_from_c_string(def)}, TYPE_STR, req)

#define clp_new_operand_default_char(name, desc, def, req) clp_new_operand_raw(name, desc, true, OPERAND_ACT_SET, (Value) {.value_char = (def)}, TYPE_CHAR, req)

#define clp_new_operand_default_double(name, desc, def, req) clp_new_operand_raw(name, desc, true, OPERAND_ACT_SET, (Value) {.value_double = (def)}, TYPE_DOUBLE, req)

#define clp_new_operand_list(name, desc, required) clp_new_operand_raw(name, desc, false, OPERAND_ACT_LIST, (Value) {0}, TYPE_STR, required)

#define clp_new_operand_kv(name, desc, required) clp_new_operand_raw(name, desc, false, OPERAND_ACT_KV, (Value) {0}, TYPE_STR, required)

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

/* Allocate and initialise a new Command on the heap; returns the pointer.
 *
 *   code        — user-defined integer identifier; useful for switch dispatch
 *                 after clp_parse_args returns the matched subcommand.
 *   name        — command name as it appears in argv (e.g. "push").
 *                 Must not be NULL.
 *   description — shown in --help output; NULL is accepted (shown as
 *                 "no associated description").
 *
 * The returned pointer is owned by the caller.  Pass it to clp_cleanup to
 * release the command and all its descendant resources.  name must not be NULL.
 */
Command *clp_new_command(int code, char *name, char *description);

/* Attach sub_command as a child of command.
 *
 * Sets sub_command->parent_command = command.  A command cannot have both
 * operands and subcommands; attempting to do so exits.
 *
 * command and sub_command must not be NULL. Exits if command already has operands registered.
 */
void clp_add_command_sub_command(Command *command, Command *sub_command);

/* Register an option on a command.
 *
 * command and command_option must not be NULL.
 * Duplicate long or short names on the same command cause an exit.
 */
void clp_add_command_option(Command *command, Option *command_option);

/* Register an operand on a command.
 *
 * Operands are consumed in registration order.  A required operand must not
 * follow an optional one.  A duplicate operand name emits a warning to stderr
 * but does not exit.  A command cannot have both operands and subcommands.
 *
 * command and command_operand must not be NULL.
 * Exits if subcommands are already present or on a required-after-optional ordering violation.
 */
void clp_add_command_operand(Command *command, Operand *command_operand);

/* Low-level option allocator — prefer the clp_new_option* macros.
 *
 * Allocates a new Option on the heap and returns the pointer.  The returned
 * Option is owned by the command it is added to; clp_cleanup frees it.
 *
 *   long_name         — long option name without the "--" prefix, or NULL for
 *                       a short-only option.  Must start with a letter and
 *                       contain only [a-z A-Z 0-9 -].  "help" is reserved.
 *   short_name        — single-character string (e.g. "v"), or NULL for a
 *                       long-only option.  Must be alphanumeric [a-z A-Z 0-9].
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
 * At least one of long_name or short_name must not be NULL.
 * Exits on invalid names, reserved name, or OPT_ACT_COUNT paired with a type other than TYPE_USIZE.
 */
Option *clp_new_option_raw(char *long_name, char *short_name, char *description, bool has_default_value, OptAction action, Value value, Type type, bool required, bool global);

/* Low-level operand allocator — prefer the clp_new_operand* macros.
 *
 * Allocates a new Operand on the heap and returns the pointer.  The returned
 * Operand is owned by the command it is added to; clp_cleanup frees it.
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
 * name must not be NULL. Exits on an empty or invalid name.
 */
Operand *clp_new_operand_raw(char *name, char *description, bool has_default_value, OperandAction action, Value value, Type type, bool required);

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

/* Recursively release all resources allocated inside the command tree.
 */
void clp_cleanup(Command *root);
#endif