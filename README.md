# clp — Command-Line Parser

A C static library for building command-line interfaces with options, operands, subcommands, and global options.

Parsed values are written straight into **variables you own**: clp never stores a value and never assigns a default. Whatever a variable holds before parsing is its default.

## Building

See [BUILDING.md](BUILDING.md).

---

## Concepts

### Command

A command owns a set of options and either operands **or** subcommands (never both). Every program has a root command; subcommands are nested below it.

```c
Command *root, *push;
ClpError err;
if (clp_new_command(&root, 0, "git", "the stupid content tracker", NULL, NULL, &err) != CLP_OK ||
    clp_new_command(&push, 1, "push", "update remote refs", NULL, NULL, &err) != CLP_OK ||
    clp_add_command_sub_command(root, push, &err) != CLP_OK)
    { /* handle err */ }
```

After parsing, the `Command **command` out-parameter points to the deepest dispatched subcommand (or stays `NULL` if no command token was found).

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

An option marked `global=true` is visible to every descendant command. It is registered on the parent and can be passed before or after the subcommand token (`prog -v push` and `prog push -v` are the same). When an option name is not found on the command being parsed, clp looks it up among the global options of its ancestors, nearest first, so a subcommand's own option shadows an ancestor global with the same name. Globals are never copied into the subcommand: each option stays owned by the command it was added to. Required global options are checked on the dispatched command, even when it gets no arguments.

### Built-in help

`--help` and `-h` (when the user has not registered their own `-h`) print the command description, options, subcommands, and operands. `clp_parse_args` then exits with `EXIT_SUCCESS`; `clp_try_parse_args` returns `CLP_HELP_SHOWN`. The name `help` is reserved and cannot be used as a long option name.

---

## Values: `dest`, defaults and `value_set`

Every option and operand is bound to a `dest`, a pointer to a variable you own. Parsing writes into it:

- **Default**: initialise `dest` before parsing. If the option is not given, it keeps that value.
- **`value_set`**: `Option.value_set` / `Operand.value_set` is `true` once a value was given and accepted, so you can tell "given" from "default".
- **Rejected values never reach `dest`**: a value is converted and checked into scratch storage first, and only copied into `dest` once accepted. `--ttl 12x` leaves `ttl` untouched.
- **Ownership**: `dest` must outlive parsing. clp never frees it; `clp_cleanup` releases only clp's own objects.

### Value types (SET)

The type is deduced from the `dest` pointer at compile time; any other pointer type does not compile.

| `dest` type     | Enum          | Notes                                                 |
| --------------- | ------------- | ----------------------------------------------------- |
| `bool *`        | `TYPE_BOOL`   | flag: takes no argument, sets `*dest = true`          |
| `char *`        | `TYPE_CHAR`   | exactly one character                                 |
| `DStringView *` | `TYPE_STR`    | `.data` points into `argv` and is NUL-terminated      |
| `long *`        | `TYPE_LONG`   |                                                       |
| `usize *`       | `TYPE_USIZE`  |                                                       |
| `double *`      | `TYPE_DOUBLE` |                                                       |
| _(ctx)_         | `TYPE_CUSTOM` | no `dest`: the argument goes to a transform with its `ctx` (see below) |

## Option actions

| Action          | Macro                  | `dest` type       | Notes                                           |
| --------------- | ---------------------- | ----------------- | ----------------------------------------------- |
| `OPT_ACT_SET`   | `clp_new_option`       | see value types   | stores one value; `bool *` needs no argument    |
| `OPT_ACT_COUNT` | `clp_new_option_count` | `usize *`         | increments `*dest` each occurrence; no argument |
| `OPT_ACT_LIST`  | `clp_new_option_list`  | `DDynArray *`     | comma-split into `DStringView` elements         |
| `OPT_ACT_KV`    | `clp_new_option_kv`    | `DUnorderedMap *` | `key=value` pairs, comma-separated              |

## Operand actions

| Action             | Macro                  | `dest` type       | Notes                                  |
| ------------------ | ---------------------- | ----------------- | -------------------------------------- |
| `OPERAND_ACT_SET`  | `clp_new_operand`      | see value types   | one positional value                   |
| `OPERAND_ACT_LIST` | `clp_new_operand_list` | `DDynArray *`     | consumes all remaining positional args |
| `OPERAND_ACT_KV`   | `clp_new_operand_kv`   | `DUnorderedMap *` | `key=value` positional arg             |

### LIST and KV collections

Collections are owned by the caller. Create them with `clp_list_init` / `clp_kv_init`, which use the element types the parser expects, and release them with `clp_list_destroy` / `clp_kv_destroy` when you are done:

```c
DDynArray     files;
DUnorderedMap env;
if (clp_list_init(&files, &err) != CLP_OK || clp_kv_init(&env, &err) != CLP_OK)
    /* CLP_ERR_ALLOC */;
/* ... parse ... */
clp_list_destroy(&files);
clp_kv_destroy(&env);
```

Both init functions return `CLP_OK` or `CLP_ERR_ALLOC`. `clp.h` declares `DUnorderedMap`, so KV users only need `d_unordered_map.h` to read the map (`d_unordered_map_get`).

Stored `DStringView`s point into `argv` and stay valid as long as `argv` does.

---

## Quick start

```c
#include "clp.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    (void) argc;

    bool        verbose = false; /* default */
    long        jobs    = 1;     /* default */
    DStringView file;
    ClpError    err;
    Command    *root;
    Option     *verbose_opt, *jobs_opt;
    Operand    *file_op;

    if (clp_new_command(&root, 0, "prog", "my program", NULL, NULL, &err) != CLP_OK)
    {
        clp_print_error(stderr, &err);
        return 1;
    }
    if (clp_new_option(&verbose_opt, "verbose", "v", "enable verbose output", &verbose, false, false, &err) != CLP_OK ||
        clp_add_command_option(root, verbose_opt, &err) != CLP_OK ||
        clp_new_option(&jobs_opt, "jobs", "j", "number of jobs", &jobs, false, false, &err) != CLP_OK ||
        clp_add_command_option(root, jobs_opt, &err) != CLP_OK ||
        clp_new_operand(&file_op, "file", "input file", &file, true, &err) != CLP_OK ||
        clp_add_command_operand(root, file_op, &err) != CLP_OK)
    {
        /* an option/operand whose clp_add_* failed is still yours:
           release it with clp_free_option / clp_free_operand */
        clp_print_error(stderr, &err);
        clp_cleanup(root);
        return 1;
    }

    Command *cmd = NULL;
    clp_parse_args(root, argv, &cmd); /* exits on error or --help */

    if (verbose)
        printf("verbose mode on\n");
    if (jobs_opt->value_set)
        printf("jobs given on the command line\n");
    printf("file: %s, jobs: %ld\n", file.data, jobs);

    clp_cleanup(root);
    return 0;
}
```

---

## Examples by feature

The snippets below omit error checks on the builders for brevity; every builder returns a `ClpStatus` you should check.

### Subcommands

```c
Command *root, *add, *rm;
clp_new_command(&root, 0, "prog", "description", NULL, NULL, &err);
clp_new_command(&add, 1, "add", "add a file", NULL, NULL, &err);
clp_new_command(&rm, 2, "rm", "remove a file", NULL, NULL, &err);
clp_add_command_sub_command(root, add, &err);
clp_add_command_sub_command(root, rm, &err);

Command *cmd = NULL;
clp_parse_args(root, argv, &cmd);
switch (cmd ? cmd->code : -1)
{
case 1: /* add */ break;
case 2: /* rm  */ break;
}
clp_cleanup(root);
```

### Count option (`-d`, `-ddd`, `--debug`)

```c
usize   debug = 0; /* counting starts from the initial value */
Option *opt;
clp_new_option_count(&opt, "debug", "d", "increase debug level", &debug, false, &err);
clp_add_command_option(root, opt, &err);
/* after parse: debug == number of -d flags */
```

### List option (`--files a,b,c`)

```c
DDynArray files;
clp_list_init(&files, &err);
clp_new_option_list(&opt, "files", "f", "input files", &files, false, false, &err);
clp_add_command_option(root, opt, &err);
/* after parse: files holds one DStringView per item */
```

### Key=value option (`--env HOST=localhost,PORT=8080`)

```c
DUnorderedMap env;
clp_kv_init(&env, &err);
clp_new_option_kv(&opt, "env", "e", "environment variables", &env, false, false, &err);
clp_add_command_option(root, opt, &err);

/* lookup after parse */
DStringView  key = D_STRING_VIEW_FROM_LITERAL("HOST");
DStringView *val = d_unordered_map_get(&env, &key);
```

### List operand (consumes all remaining positional args)

```c
DDynArray files;
Operand  *op;
clp_list_init(&files, &err);
clp_new_operand_list(&op, "files", "source files", &files, false, &err);
clp_add_command_operand(root, op, &err);
/* prog a.c b.c c.c → files has 3 elements */
```

### Option with a default value

```c
usize jobs = 4; /* the default is just the initial value */
clp_new_option(&opt, "jobs", "j", "parallel jobs", &jobs, false, false, &err);
/* jobs == 4 if --jobs not supplied; opt->value_set tells whether it was */
```

### Global option

```c
/* registered on root, visible when any subcommand is dispatched */
DStringView token;
clp_new_option(&opt, "token", "t", "API token", &token, true, /*global=*/true, &err);
clp_add_command_option(root, opt, &err);
/* prog --token abc push  and  prog push --token abc  both set token */
```

---

## Input checkers

```c
typedef const char *(*ClpChecker)(const void *value);
```

A checker runs on a value right after it has been converted, **before** it is written to `dest`. Return `NULL` to accept it, or a message with static lifetime to reject it: parsing then fails with `CLP_ERR_BAD_VALUE`, the message is appended to the usual `invalid value '...' for '--x'` error, and `dest` keeps its previous value.

What `value` points to:

- **SET**: the converted value, with the same type as `dest` (`const long *` for a `long` dest, ...). It is a scratch copy, not `dest` itself.
- **LIST**: one item, as a `const DStringView *`, including each comma-separated piece of an option value.
- **KV**: one pair, as a `const ClpKeyValue *` (`{ DStringView key; DStringView value; }`; the views are not NUL-terminated). Malformed pairs are rejected with `CLP_ERR_BAD_KV` first.
- Never run for bool flags or count options.

```c
static const char *jobs_range(const void *p)
{
    long jobs = *(const long *) p;
    return (jobs >= 1 && jobs <= 64) ? NULL : "must be in [1 64]";
}

static const char *check_env(const void *p)
{
    const ClpKeyValue *kv = p;
    return kv->key.size <= 32 ? NULL : "key too long";
}

long jobs = 4;
clp_new_option_checked(&opt, "jobs", "j", "worker count", &jobs, false, false, jobs_range, &err);
clp_new_option_list_checked(&opt, "tag", "t", "tags", &tags, false, false, check_tag, &err);
clp_new_option_kv_checked(&opt, "env", "e", "environment", &env, false, false, check_env, &err);
clp_new_operand_checked(&op, "port", "port to bind", &port, true, port_range, &err);
clp_new_operand_list_checked(&op, "files", "input files", &files, true, check_file, &err);
clp_new_operand_kv_checked(&op, "vars", "variables", &vars, true, check_env, &err);
```

## Transforms (custom parsing)

When an argument needs parsing the built-in types cannot express (an enum, a hex pattern, a set of flags, ...), use a transform. It takes **no `dest`**: it receives the raw argument plus a `ctx` pointer given at registration, and decides what to do with them — usually parse the argument and store the result through `ctx`.

```c
typedef const char *(*ClpTransform)(void *ctx, const char *arg);
```

- `arg` is the raw, NUL-terminated argument, pointing into `argv`.
- `ctx` is passed through untouched (it may be `NULL`). Because the target comes in through `ctx`, one transform can serve several options, with no global state.
- Return `NULL` to accept, or a message with static lifetime to reject: parsing fails with `CLP_ERR_BAD_VALUE` and `value_set` stays `false`. Anything the transform stored before rejecting is its own responsibility.
- clp only tracks `value_set` and `required`; it stores nothing.
- SET only, no checker (the transform validates itself). Shown as `<value>` in `--help`.

```c
typedef enum { TS_NONE, TS_ONLY, TS_ADDR } TsType;

static const char *parse_timestamp(void *ctx, const char *arg)
{
    TsType *out = ctx;
    if (strcmp(arg, "tsonly") == 0)
        *out = TS_ONLY;
    else if (strcmp(arg, "tsaddr") == 0)
        *out = TS_ADDR;
    else
        return "expected \"tsonly\" or \"tsaddr\"";
    return NULL;
}

TsType ip_ts  = TS_NONE; /* defaults are the initial values, as with dest */
TsType tcp_ts = TS_NONE;
/* one transform, two options: each result goes to its own ctx */
clp_new_option_transform(&opt, "ip-timestamp", NULL, "IP timestamp of type FLAG", parse_timestamp, &ip_ts, false, false, &err);
clp_new_option_transform(&opt2, "tcp-timestamp", NULL, "TCP timestamp of type FLAG", parse_timestamp, &tcp_ts, false, false, &err);
clp_new_operand_transform(&op, "mode", "run mode", parse_mode, &mode, true, &err);
```

---

## Command validators (post-parse)

Checkers and transforms see one value at a time. Rules that span several arguments ("`-f` and `-i` are incompatible", "`--tsaddr` needs `--timestamp`") go in a validator, given to `clp_new_command` with a `ctx`:

```c
typedef const char *(*ClpValidator)(void *ctx);
```

- Runs once the command's arguments are all parsed and its required arguments are present. It does not run if parsing failed earlier, and it runs even when the command got no arguments.
- Only the validator of the command parsing ends on runs (the one returned through `command`), not its ancestors'.
- `ctx` is passed through untouched.
- Return `NULL` to accept, or a message with static lifetime to reject: parsing fails with `CLP_ERR_VALIDATOR`, `err->command` is the command and the message reads `command <name>: <your message>`.

```c
typedef struct { Option *flood, *interval; } PingRules;

static const char *check_ping(void *ctx)
{
    PingRules *r = ctx;
    if (r->flood->value_set && r->interval->value_set)
        return "-f and -i are incompatible";
    return NULL;
}

PingRules rules;
clp_new_command(&root, 0, "ping", "send ICMP ECHO_REQUEST", check_ping, &rules, &err);
clp_new_option(&rules.flood, "flood", "f", "flood ping", &flood, false, false, &err);
clp_new_option(&rules.interval, "interval", "i", "wait between packets", &interval, false, false, &err);
```

---

## API reference

### Lifecycle

```c
/* Builders never exit: they return a ClpStatus and fill err (may be NULL).
   Constructors set *out to NULL on failure. */
ClpStatus clp_new_command(Command **out, int code, char *name, char *description, ClpValidator validator, void *ctx, ClpError *err);
ClpStatus clp_add_command_option(Command *command, Option *option, ClpError *err);
ClpStatus clp_add_command_operand(Command *command, Operand *operand, ClpError *err);
ClpStatus clp_add_command_sub_command(Command *command, Command *sub_command, ClpError *err);

ClpStatus   clp_try_parse_args(Command *root, char **argv, Command **command, ClpError *err); /* never exits */
void        clp_parse_args(Command *root, char **argv, Command **command);                   /* exits on error */
void        clp_print_error(FILE *stream, const ClpError *err);
const char *clp_status_to_str(ClpStatus code);

void clp_cleanup(Command *root);         /* frees the tree and everything added to it; never touches dests */
void clp_free_option(Option *option);    /* only for an option not added to a command (NULL is a no-op) */
void clp_free_operand(Operand *operand); /* only for an operand not added to a command (NULL is a no-op) */

/* LIST / KV collections (caller-owned) */
ClpStatus clp_list_init(DDynArray *list, ClpError *err);    /* CLP_OK or CLP_ERR_ALLOC */
ClpStatus clp_kv_init(DUnorderedMap *map, ClpError *err);   /* CLP_OK or CLP_ERR_ALLOC */
void      clp_list_destroy(DDynArray *list);
void      clp_kv_destroy(DUnorderedMap *map);
```

Ownership: an option/operand belongs to the command once `clp_add_command_*` succeeds, and `clp_cleanup` frees it. If the add fails it is still yours — release it with `clp_free_option` / `clp_free_operand`.

### Lookup

```c
Option  *clp_get_option_by_short(Command *command, char shrt);
Option  *clp_get_option_by_long (Command *command, DStringView lng);
Operand *clp_get_operand        (Command *command, DStringView name);
```

### Option macros

Every macro takes `out` first and `err` last, and returns a `ClpStatus`.

```c
clp_new_option          (out, ln, sn, desc, dest, required, global, err)          /* SET; type from dest */
clp_new_option_count    (out, ln, sn, desc, dest, global, err)                    /* dest: usize * */
clp_new_option_list     (out, ln, sn, desc, dest, required, global, err)          /* dest: DDynArray * */
clp_new_option_kv       (out, ln, sn, desc, dest, required, global, err)          /* dest: DUnorderedMap * */

clp_new_option_checked     (out, ln, sn, desc, dest, required, global, checker, err)
clp_new_option_list_checked(out, ln, sn, desc, dest, required, global, checker, err)
clp_new_option_kv_checked  (out, ln, sn, desc, dest, required, global, checker, err)

clp_new_option_transform(out, ln, sn, desc, transform, ctx, required, global, err) /* no dest; ctx → transform */
```

### Operand macros

```c
clp_new_operand     (out, name, desc, dest, required, err)                        /* SET; type from dest */
clp_new_operand_list(out, name, desc, dest, required, err)                        /* dest: DDynArray * */
clp_new_operand_kv  (out, name, desc, dest, required, err)                        /* dest: DUnorderedMap * */

clp_new_operand_checked     (out, name, desc, dest, required, checker, err)
clp_new_operand_list_checked(out, name, desc, dest, required, checker, err)
clp_new_operand_kv_checked  (out, name, desc, dest, required, checker, err)

clp_new_operand_transform(out, name, desc, transform, ctx, required, err)         /* no dest; ctx → transform */
```

### Inserting a table of options / operands

`clp_insert_opts` and `clp_insert_operands` create and register a whole array in one call. Each `ClpOptionInfo` / `ClpOperandInfo` holds the arguments of `clp_new_option_raw` / `clp_new_operand_raw`, with the same rules (and, like the raw constructors, no check that `dest` matches `type`).

```c
typedef struct ClpOptionInfo {
    char *long_name; char *short_name; char *description; void *dest;
    OptAction action; Type type; bool required; bool global;
    ClpChecker checker; ClpTransform transform;
} ClpOptionInfo;

typedef struct ClpOperandInfo {
    char *name; char *description; void *dest;
    OperandAction action; Type type; bool required;
    ClpChecker checker; ClpTransform transform;
} ClpOperandInfo;

ClpStatus clp_insert_opts(Command *command, const ClpOptionInfo *infos, usize count, ClpError *err);
ClpStatus clp_insert_operands(Command *command, const ClpOperandInfo *infos, usize count, ClpError *err);
```

```c
ClpOptionInfo opts[] = {
    {"verbose", "v", "verbose output", &verbose, OPT_ACT_SET, TYPE_BOOL, false, false, NULL, NULL},
    {"jobs",    "j", "worker count",   &jobs,    OPT_ACT_SET, TYPE_LONG, false, false, jobs_range, NULL},
};
ClpOperandInfo ops[] = {
    {"host", "host to reach", &host, OPERAND_ACT_SET, TYPE_STR, true, NULL, NULL},
};
if (clp_insert_opts(root, opts, sizeof(opts) / sizeof(*opts), &err) != CLP_OK ||
    clp_insert_operands(root, ops, sizeof(ops) / sizeof(*ops), &err) != CLP_OK)
    ...
```

Entries are inserted in order and insertion stops at the first failure: `err` is filled and its status returned, the entry that failed is released, and the entries inserted before it stay on the command (freed by `clp_cleanup`). `infos` may be `NULL` when `count` is `0`.

### Raw constructors (full control)

The macros cover every combination; the raw constructors do not check that `dest` matches `type`, so prefer the macros.

```c
ClpStatus clp_new_option_raw(Option **out, char *long_name, char *short_name, char *description,
                             void *dest, OptAction action, Type type, bool required, bool global,
                             ClpChecker checker, ClpTransform transform, ClpError *err);

ClpStatus clp_new_operand_raw(Operand **out, char *name, char *description,
                              void *dest, OperandAction action, Type type, bool required,
                              ClpChecker checker, ClpTransform transform, ClpError *err);
```

A `transform` requires `TYPE_CUSTOM` (and `TYPE_CUSTOM` requires a transform), the SET action, and no checker; `dest` is then passed to the transform as its `ctx` and may be `NULL`. Any other combination returns `CLP_ERR_INVALID_TYPE`.

---

## Errors

Builders and `clp_try_parse_args` never exit: they return a `ClpStatus` and fill `ClpError` (print it with `clp_print_error`). `clp_parse_args` prints the same message to **stderr** and calls `exit(EXIT_FAILURE)` instead. Passing `NULL` where a parameter must not be `NULL` is a programming error caught by `assert`.

### At registration (builders)

| Condition                                                          | Status                                 |
| ------------------------------------------------------------------ | -------------------------------------- |
| Long option name starts with `-` or `_`, or contains invalid chars | `CLP_ERR_INVALID_NAME`                 |
| Short option name is not alphanumeric or `?`                       | `CLP_ERR_INVALID_NAME`                 |
| Long option name is `"help"` (reserved)                            | `CLP_ERR_RESERVED_NAME`                |
| `OPT_ACT_COUNT` paired with a type other than `TYPE_USIZE`         | `CLP_ERR_INVALID_TYPE`                 |
| Transform / `TYPE_CUSTOM` / checker combination not allowed        | `CLP_ERR_INVALID_TYPE`                 |
| Duplicate long or short option name on the same command            | `CLP_ERR_DUPLICATE`                    |
| Command would have both operands and subcommands                   | `CLP_ERR_CONFLICT`                     |
| Required operand registered after an optional one                  | `CLP_ERR_OPERAND_ORDER`                |
| Duplicate operand name on the same command                         | accepted (warning when built with `-DWARNING`) |
| Out of memory                                                      | `CLP_ERR_ALLOC`                        |

### At parse time

| Condition                                                  | Status                                         |
| ---------------------------------------------------------- | ---------------------------------------------- |
| `--help` / `-h`                                            | `CLP_HELP_SHOWN` (`clp_parse_args` exits 0)    |
| Unknown option token                                       | `CLP_ERR_UNKNOWN_OPTION`                       |
| Option requires an argument and none was given             | `CLP_ERR_MISSING_ARG`                          |
| `--flag=value` on an option that takes no argument         | `CLP_ERR_UNEXPECTED_ARG`                       |
| Value fails conversion, or is rejected by a checker/transform | `CLP_ERR_BAD_VALUE`                         |
| KV pair missing `=`, or with an empty key or value         | `CLP_ERR_BAD_KV`                               |
| Too many positional arguments                              | `CLP_ERR_TOO_MANY_OPERANDS`                    |
| Missing required option or operand                         | `CLP_ERR_MISSING_REQUIRED` (message includes usage) |
| Rejected by the matched command's validator               | `CLP_ERR_VALIDATOR`                            |

On a parse error, `dest`s written before the error keep their values (for LIST/KV, so do the items accepted before the bad one) and transforms that already accepted an argument are not undone. The rejected value itself is never written.
