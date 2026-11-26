# clp — Command-Line Parser

A C static library for building command-line interfaces with options, operands, subcommands, and global options.

## Building

See [BUILDING.md](BUILDING.md).

---

## Concepts

### Command

A command owns a set of options and either operands **or** subcommands (never both). Every program has a root command; subcommands are nested below it.

```c
Command *root = clp_new_command(0, "git",  "the stupid content tracker");
Command *push = clp_new_command(1, "push", "update remote refs");
clp_add_command_sub_command(root, push);
```

After `clp_parse_args`, the `Command **command` out-parameter points to the deepest dispatched subcommand (or to to `NULL` if no command token was found).

### Options

Named flags prefixed with `-` (short) or `--` (long). An option may have a short name, a long name, or both.

| Syntax                   | Example                                                                                                                                                                   |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Short flag               | `-v`                                                                                                                                                                      |
| Long flag                | `--verbose`                                                                                                                                                               |
| Short + value (inline)   | `-ofile.txt`                                                                                                                                                              |
| Short + value (next arg) | `-o file.txt`                                                                                                                                                             |
| Long + value (inline)    | `--output=file.txt`                                                                                                                                                       |
| Long + value (next arg)  | `--output file.txt`                                                                                                                                                       |
| Combined short           | `-abc`, `-abVALUE` — each char processed in order; bool/count options continue to the next char; a value-taking option consumes the rest of the token as its inline value |
| Repeated count           | `-vvv` or `-v -v -v`                                                                                                                                                      |

### Operands

Positional arguments (no `-` prefix). They are assigned to registered operands in declaration order. Options may appear before, between, or after operands.

### The `--` separator

`--` terminates option parsing. Everything after it is treated as an operand, even if it starts with `-`.

### Global options

An option marked `global=true` is visible to every descendant command. It is registered on the parent but can be set by passing it before the subcommand token. Required global options are validated in the context of the command that triggered the check.

### Built-in help

`--help` and `-h` (when the user has not registered their own `-h`) print the command description, options, subcommands, and operands, then exit with `EXIT_SUCCESS`. The name `help` is reserved and cannot be used as a long option name.

---

## Option actions

| Action          | Macro                   | Value field                  | Notes                                           |
| --------------- | ----------------------- | ---------------------------- | ----------------------------------------------- |
| `OPT_ACT_SET`   | `clp_init_option`       | type-dependent               | stores one value; `TYPE_BOOL` needs no argument |
| `OPT_ACT_COUNT` | `clp_init_option_count` | `value_usize`                | increments each occurrence; no argument         |
| `OPT_ACT_LIST`  | `clp_init_option_list`  | `value_list` (`DDynArray`)   | comma-split into `DStringView` elements         |
| `OPT_ACT_KV`    | `clp_init_option_kv`    | `value_kv` (`DUnorderedMap`) | `key=value` pairs, comma-separated              |

## Operand actions

| Action             | Macro                  | Value field                  | Notes                                  |
| ------------------ | ---------------------- | ---------------------------- | -------------------------------------- |
| `OPERAND_ACT_SET`  | `clp_new_operand`      | type-dependent               | one positional value                   |
| `OPERAND_ACT_LIST` | `clp_new_operand_list` | `value_list` (`DDynArray`)   | consumes all remaining positional args |
| `OPERAND_ACT_KV`   | `clp_new_operand_kv`   | `value_kv` (`DUnorderedMap`) | `key=value` positional arg             |

## Value types

| Enum          | C type        | `Value` field         | Notes                                   |
| ------------- | ------------- | --------------------- | --------------------------------------- |
| `TYPE_BOOL`   | `bool`        | `value_bool`          |                                         |
| `TYPE_CHAR`   | `char`        | `value_char`          |                                         |
| `TYPE_STR`    | `DStringView` | `value_d_string_view` | `.data` is the null-terminated C string |
| `TYPE_LONG`   | `long`        | `value_long`          |                                         |
| `TYPE_USIZE`  | `usize`       | `value_usize`         |                                         |
| `TYPE_DOUBLE` | `double`      | `value_double`        |                                         |

---

## Quick start

```c
#include "clp.h"

int main(int argc, char **argv)
{
    (void)argc;

    Command *root = clp_new_command(0, "prog", "my program");
    Option   verbose;
    Operand  file;

    clp_init_option(&verbose, "verbose", "v", "enable verbose output",
                    TYPE_BOOL, false, false);
    clp_new_operand(&file, "file", "input file", TYPE_STR, true);

    clp_add_command_option(root, &verbose);
    clp_add_command_operand(root, &file);

    Command *cmd = NULL;
    clp_parse_args(root, argv, &cmd);

    if (verbose.value_set)
        printf("verbose mode on\n");
    printf("file: %s\n", file.value.value_d_string_view.data);

    clp_cleanup(root);
    return 0;
}
```

---

## Examples by feature

### Subcommands

```c
Command *root = clp_new_command(0, "prog", "description");
Command *add  = clp_new_command(1, "add",  "add a file");
Command *rm   = clp_new_command(2, "rm",   "remove a file");
clp_add_command_sub_command(root, add);
clp_add_command_sub_command(root, rm);

Command *cmd = NULL;
clp_parse_args(root, argv, &cmd);

if (cmd)
{
    switch (cmd ? cmd->code : -1) {
    case 1: /* add */ break;
    case 2: /* rm  */ break;
    }
}
clp_cleanup(root);
```

### Count option (`-v`, `-vvv`, `--verbose`)

```c
Option debug;
clp_init_option_count(&debug, "debug", "d", "increase debug level", false);
clp_add_command_option(&root, &debug);
/* after parse: debug.value.value_usize == number of -d flags */
```

### List option (`--files a,b,c`)

```c
Option files;
clp_init_option_list(&files, "files", "f", "input files", false, false);
clp_add_command_option(&root, &files);
/* after parse: iterate files.value.value_list (DDynArray of DStringView) */
```

### Key=value option (`--env HOST=localhost,PORT=8080`)

```c
Option env;
clp_init_option_kv(&env, "env", "e", "environment variables", false, false);
clp_add_command_option(&root, &env);

/* lookup after parse */
DStringView key = D_STRING_VIEW_FROM_LITERAL("HOST");
DStringView *val = d_unordered_map_get(&env.value.value_kv, &key);
```

### List operand (consumes all remaining positional args)

```c
Operand files;
clp_new_operand_list(&files, "files", "source files", false);
clp_add_command_operand(&root, &files);
/* prog a.c b.c c.c → files.value.value_list has 3 elements */
```

### Option with default value

```c
Option jobs;
clp_init_option_default_usize(&jobs, "jobs", "j", "parallel jobs", 4, false, false);
/* jobs.value.value_usize == 4 if --jobs not supplied */
```

### Global option

```c
/* registered on root, visible when any subcommand is dispatched */
Option token;
clp_init_option(&token, "token", "t", "API token", TYPE_STR, true, /*global=*/true);
clp_add_command_option(&root, &token);
/* prog --token abc push  →  token.value_set == true inside 'push' handler */
```

---

## API reference

### Lifecycle

```c
/* Builders never exit: they return a ClpStatus and fill err (may be NULL). */
ClpStatus clp_new_command(Command **out, int code, char *name, char *description, ClpError *err);
ClpStatus clp_add_command_option(Command *command, Option *option, ClpError *err);
ClpStatus clp_add_command_operand(Command *command, Operand *operand, ClpError *err);
ClpStatus clp_add_command_sub_command(Command *command, Command *sub_command, ClpError *err);

ClpStatus clp_try_parse_args(Command *root, char **argv, Command **command, ClpError *err);
void      clp_parse_args(Command *root, char **argv, Command **command); /* exits on error */
void      clp_print_error(FILE *stream, const ClpError *err);
const char *clp_status_to_str(ClpStatus code);
void      clp_cleanup(Command *root);
```

### Lookup

```c
Option  *clp_get_option_by_short(Command *command, char shrt);
Option  *clp_get_option_by_long (Command *command, DStringView lng);
Operand *clp_get_operand        (Command *command, DStringView name);
```

### Raw initializers (full control)

```c
ClpStatus clp_new_option_raw(Option **out, char *long_name, char *short_name,
                             char *description, bool has_default_value,
                             OptAction action, Value value,
                             Type type, bool required, bool global,
                             ClpChecker checker, ClpError *err);

ClpStatus clp_new_operand_raw(Operand **out, char *name, char *description,
                              bool has_default_value,
                              OperandAction action, Value value,
                              Type type, bool required,
                              ClpChecker checker, ClpError *err);
/* every clp_new_option_* / clp_new_operand_* macro takes `out` first and `err` last */
```

### Option convenience macros

```c
/* OPT_ACT_SET — no default */
clp_init_option(opt, long_name, short_name, desc, type, required, global)

/* OPT_ACT_SET — with default */
clp_init_option_default_bool  (opt, ln, sn, desc, def, req, glob)
clp_init_option_default_char  (opt, ln, sn, desc, def, req, glob)
clp_init_option_default_str   (opt, ln, sn, desc, def, req, glob)
clp_init_option_default_long  (opt, ln, sn, desc, def, req, glob)
clp_init_option_default_usize (opt, ln, sn, desc, def, req, glob)
clp_init_option_default_double(opt, ln, sn, desc, def, req, glob)

/* OPT_ACT_COUNT — always TYPE_USIZE, never required */
clp_init_option_count(opt, ln, sn, desc, glob)

/* OPT_ACT_LIST */
clp_init_option_list(opt, ln, sn, desc, required, glob)

/* OPT_ACT_KV */
clp_init_option_kv(opt, ln, sn, desc, required, glob)
```

### Operand convenience macros

```c
/* OPERAND_ACT_SET — no default */
clp_new_operand(op, name, desc, type, required)

/* OPERAND_ACT_SET — with default */
clp_new_operand_default_bool  (op, name, desc, def, req)
clp_new_operand_default_char  (op, name, desc, def, req)
clp_new_operand_default_str   (op, name, desc, def, req)
clp_new_operand_default_long  (op, name, desc, def, req)
clp_new_operand_default_usize (op, name, desc, def, req)
clp_new_operand_default_double(op, name, desc, def, req)

/* OPERAND_ACT_LIST */
clp_new_operand_list(op, name, desc, required)

/* OPERAND_ACT_KV */
clp_new_operand_kv(op, name, desc, required)
```

### Input checkers

```c
typedef const char *(*ClpChecker)(const Value *value);
```

A checker runs on a value right after it has been converted to its type.
Return `NULL` to accept it, or a message with static lifetime to reject it:
parsing then fails with `CLP_ERR_BAD_VALUE` and the message is appended to the
usual `invalid value '...' for '--x'` error.

- SET options/operands: the parsed value is checked.
- LIST options/operands: each item is checked (as `value_d_string_view`),
  including each comma-separated piece of an option value.
- KV options/operands: each `key=value` pair is checked (as `value_key_value`,
  a `ClpKeyValue { DStringView key; DStringView value; }`; the views are not
  NUL-terminated). Malformed pairs are rejected with `CLP_ERR_BAD_KV` first.
- Defaults and count values are never checked.

```c
static const char *jobs_range(const Value *v)
{
    return (v->value_long >= 1 && v->value_long <= 64) ? NULL : "must be in [1 64]";
}

clp_new_option_checked(&opt, "jobs", "j", "worker count", TYPE_LONG, false, false, jobs_range, &err);
clp_new_option_default_long_checked(&opt, "jobs", "j", "worker count", 4, false, false, jobs_range, &err);
clp_new_option_list_checked(&opt, "tag", "t", "tags", false, false, check_tag, &err);
clp_new_operand_checked(&op, "port", "port to bind", TYPE_USIZE, true, port_range, &err);
clp_new_operand_list_checked(&op, "files", "input files", true, check_file, &err);
clp_new_option_kv_checked(&opt, "env", "e", "environment", false, false, check_env, &err);
clp_new_operand_kv_checked(&op, "vars", "variables", true, check_env, &err);
/* _default_{bool,char,str,long,usize,double}_checked exist for both options and operands */
```

```c
static const char *check_env(const Value *v)
{
    const ClpKeyValue *kv = &v->value_key_value;
    return kv->key.size <= 32 ? NULL : "key too long";
}
```

---

## Constraints and error behaviour

All error conditions print a message to **stderr** and call `exit(EXIT_FAILURE)`.

| Condition                                                          | Error                               |
| ------------------------------------------------------------------ | ----------------------------------- |
| Long option name starts with `-` or `_`, or contains invalid chars | exits                               |
| Short option name is not alphanumeric or `?`                       | exits                               |
| Long option name is `"help"` (reserved)                            | exits                               |
| `OPT_ACT_COUNT` paired with a type other than `TYPE_USIZE`         | exits                               |
| Duplicate long or short option name on the same command            | exits                               |
| Command has both operands and subcommands                          | exits                               |
| Required operand registered after an optional one                  | exits                               |
| Duplicate operand name on the same command                         | warning on stderr (no exit)         |
| Unknown option token at parse time                                 | exits                               |
| Option value fails type conversion                                 | exits                               |
| Too many positional arguments                                      | exits                               |
| Missing required option or operand                                 | exits, prints usage + `--help` hint |
| KV pair missing `=`                                                | exits                               |
| KV pair has empty key or empty value                               | exits                               |
