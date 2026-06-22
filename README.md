# clp — Command-Line Parser

A C static library for building command-line interfaces with options, operands, subcommands, and global options.

## Building

See [BUILDING.md](BUILDING.md).

---

## Concepts

### Command

A command owns a set of options and either operands **or** subcommands (never both). Every program has a root command; subcommands are nested below it.

```c
Command root, push;
clp_init_command(&root, 0, "git",  "the stupid content tracker");
clp_init_command(&push, 1, "push", "update remote refs");
clp_add_command_sub_command(&root, &push);
```

After `clp_parse_args`, the `Command **command` out-parameter points to the deepest dispatched subcommand (or stays `NULL` if no subcommand token was found).

### Options

Named flags prefixed with `-` (short) or `--` (long). An option may have a short name, a long name, or both.

| Syntax | Example |
|--------|---------|
| Short flag | `-v` |
| Long flag | `--verbose` |
| Short + value (inline) | `-ofile.txt` |
| Short + value (next arg) | `-o file.txt` |
| Long + value (inline) | `--output=file.txt` |
| Long + value (next arg) | `--output file.txt` |
| Combined short | `-abc`, `-abVALUE` — each char processed in order; bool/count options continue to the next char; a value-taking option consumes the rest of the token as its inline value |
| Repeated count | `-vvv` or `-v -v -v` |

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

| Action | Macro | Value field | Notes |
|--------|-------|-------------|-------|
| `OPT_ACT_SET` | `clp_init_option` | type-dependent | stores one value; `TYPE_BOOL` needs no argument |
| `OPT_ACT_COUNT` | `clp_init_option_count` | `value_usize` | increments each occurrence; no argument |
| `OPT_ACT_LIST` | `clp_init_option_list` | `value_list` (`DDynArray`) | comma-split into `DStringView` elements |
| `OPT_ACT_KV` | `clp_init_option_kv` | `value_kv` (`DUnorderedMap`) | `key=value` pairs, comma-separated |

## Operand actions

| Action | Macro | Value field | Notes |
|--------|-------|-------------|-------|
| `OPND_ACT_SET` | `clp_init_opnd` | type-dependent | one positional value |
| `OPND_ACT_LIST` | `clp_init_opnd_list` | `value_list` (`DDynArray`) | consumes all remaining positional args |
| `OPND_ACT_KV` | `clp_init_opnd_kv` | `value_kv` (`DUnorderedMap`) | `key=value` positional arg |

## Value types

| Enum | C type | `Value` field | Notes |
|------|--------|---------------|-------|
| `TYPE_BOOL` | `bool` | `value_bool` | |
| `TYPE_CHAR` | `char` | `value_char` | |
| `TYPE_STR` | `DStringView` | `value_d_string_view` | `.data` is the null-terminated C string |
| `TYPE_LONG` | `long` | `value_long` | |
| `TYPE_USIZE` | `usize` | `value_usize` | |
| `TYPE_DOUBLE` | `double` | `value_double` | |

---

## Quick start

```c
#include "clp.h"

int main(int argc, char **argv)
{
    (void)argc;

    Command root;
    Option  verbose;
    Operand file;

    clp_init_command(&root, 0, "prog", "my program");

    clp_init_option(&verbose, "verbose", "v", "enable verbose output",
                    TYPE_BOOL, false, false);
    clp_init_opnd(&file, "file", "input file", TYPE_STR, true);

    clp_add_command_option(&root, &verbose);
    clp_add_command_operand(&root, &file);

    Command *cmd = NULL;
    clp_parse_args(&root, argv, &cmd);

    if (verbose.value_set)
        printf("verbose mode on\n");
    printf("file: %s\n", file.value.value_d_string_view.data);

    clp_cleanup(&root);
    return 0;
}
```

---

## Examples by feature

### Subcommands

```c
Command root, add, rm;
clp_init_command(&root, 0, "prog", "description");
clp_init_command(&add,  1, "add",  "add a file");
clp_init_command(&rm,   2, "rm",   "remove a file");
clp_add_command_sub_command(&root, &add);
clp_add_command_sub_command(&root, &rm);

Command *cmd = NULL;
clp_parse_args(&root, argv, &cmd);

switch (cmd ? cmd->code : -1) {
case 1: /* add */ break;
case 2: /* rm  */ break;
}
clp_cleanup(&root);
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
clp_init_opnd_list(&files, "files", "source files", false);
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
void clp_init_command(Command *command, int code, char *name, char *description);
void clp_add_command_option(Command *command, Option *option);
void clp_add_command_operand(Command *command, Operand *operand);
void clp_add_command_sub_command(Command *command, Command *sub_command);
void clp_parse_args(Command *root, char **argv, Command **command);
void clp_cleanup(Command *root);
```

### Lookup

```c
Option  *clp_get_option_by_short(Command *command, char shrt);
Option  *clp_get_option_by_long (Command *command, DStringView lng);
Operand *clp_get_operand        (Command *command, DStringView name);
```

### Raw initializers (full control)

```c
void clp_init_option_raw(Option  *opt, char *long_name, char *short_name,
                         char *description, bool has_default_value,
                         OptAction action, Value value,
                         Type type, bool required, bool global);

void clp_init_operand_raw  (Operand *op,  char *name, char *description,
                         bool has_default_value,
                         OpndAction action, Value value,
                         Type type, bool required);
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
/* OPND_ACT_SET — no default */
clp_init_opnd(op, name, desc, type, required)

/* OPND_ACT_SET — with default */
clp_init_opnd_default_bool  (op, name, desc, def, req)
clp_init_opnd_default_char  (op, name, desc, def, req)
clp_init_opnd_default_str   (op, name, desc, def, req)
clp_init_opnd_default_long  (op, name, desc, def, req)
clp_init_opnd_default_usize (op, name, desc, def, req)
clp_init_opnd_default_double(op, name, desc, def, req)

/* OPND_ACT_LIST */
clp_init_opnd_list(op, name, desc, required)

/* OPND_ACT_KV */
clp_init_opnd_kv(op, name, desc, required)
```

---

## Constraints and error behaviour

All error conditions print a message to **stderr** and call `exit(EXIT_FAILURE)`.

| Condition | Error |
|-----------|-------|
| Long option name starts with `-` or `_`, or contains invalid chars | exits |
| Short option name is not alphanumeric | exits |
| Long option name is `"help"` (reserved) | exits |
| `OPT_ACT_COUNT` paired with a type other than `TYPE_USIZE` | exits |
| Duplicate long or short option name on the same command | exits |
| Command has both operands and subcommands | exits |
| Required operand registered after an optional one | exits |
| Duplicate operand name on the same command | warning on stderr (no exit) |
| Unknown option token at parse time | exits |
| Option value fails type conversion | exits |
| Too many positional arguments | exits |
| Missing required option or operand | exits, prints usage + `--help` hint |
| KV pair missing `=` | exits |
| KV pair has empty key or empty value | exits |
