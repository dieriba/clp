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
clp_command_t *root, *push;
clp_error_t    err;
if (clp_new_command(&root, 0, "git", "the stupid content tracker", NULL, NULL, &err) != CLP_OK ||
    clp_new_command(&push, 1, "push", "update remote refs", NULL, NULL, &err) != CLP_OK ||
    clp_add_command_sub_command(root, push, &err) != CLP_OK)
    { /* handle err */ }
```

After parsing, the `clp_command_t **command` out-parameter points to the deepest dispatched subcommand (or stays `NULL` if no command token was found).

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
- **`value_set`**: `clp_option_t.value_set` / `clp_operand_t.value_set` is `true` once a value was given and accepted, so you can tell "given" from "default".
- **Rejected values never reach `dest`**: a value is converted and checked into scratch storage first, and only copied into `dest` once accepted. `--ttl 12x` leaves `ttl` untouched.
- **Ownership**: `dest` must outlive parsing. clp never frees it; `clp_cleanup` releases only clp's own objects.

### Value types (SET)

The type is deduced from the `dest` pointer at compile time; any other pointer type does not compile.

| `dest` type     | Enum          | Notes                                                 |
| --------------- | ------------- | ----------------------------------------------------- |
| `bool *`        | `CLP_TYPE_BOOL`   | flag: takes no argument, sets `*dest = true`          |
| `char *`        | `CLP_TYPE_CHAR`   | exactly one character                                 |
| `d_string_view_t *` | `CLP_TYPE_STR`    | `.data` points into `argv` and is NUL-terminated      |
| `long *`        | `CLP_TYPE_LONG`   |                                                       |
| `usize *`       | `CLP_TYPE_USIZE`  |                                                       |
| `double *`      | `CLP_TYPE_DOUBLE` |                                                       |
| _(ctx)_         | `CLP_TYPE_CUSTOM` | no `dest`: the argument goes to a transform with its `ctx` (see below) |

## Option actions

| Action          | Macro                  | `dest` type       | Notes                                           |
| --------------- | ---------------------- | ----------------- | ----------------------------------------------- |
| `CLP_OPT_ACT_SET`   | `clp_new_option`       | see value types   | stores one value; `bool *` needs no argument    |
| `CLP_OPT_ACT_COUNT` | `clp_new_option_count` | `usize *`         | increments `*dest` each occurrence; no argument |
| `CLP_OPT_ACT_LIST`  | `clp_new_option_list`  | `d_dyn_array_t *`     | comma-split into `d_string_view_t` elements         |
| `CLP_OPT_ACT_KV`    | `clp_new_option_kv`    | `d_unordered_map_t *` | `key=value` pairs, comma-separated              |

## Operand actions

| Action             | Macro                  | `dest` type       | Notes                                  |
| ------------------ | ---------------------- | ----------------- | -------------------------------------- |
| `CLP_OPERAND_ACT_SET`  | `clp_new_operand`      | see value types   | one positional value                   |
| `CLP_OPERAND_ACT_LIST` | `clp_new_operand_list` | `d_dyn_array_t *`     | consumes all remaining positional args |
| `CLP_OPERAND_ACT_KV`   | `clp_new_operand_kv`   | `d_unordered_map_t *` | `key=value` positional arg             |

### LIST and KV collections

Collections are owned by the caller. Create them with `clp_list_init` / `clp_kv_init`, which use the element types the parser expects, and release them with `clp_list_destroy` / `clp_kv_destroy` when you are done:

```c
d_dyn_array_t     files;
d_unordered_map_t env;
if (clp_list_init(&files, &err) != CLP_OK || clp_kv_init(&env, &err) != CLP_OK)
    /* CLP_ERR_ALLOC */;
/* ... parse ... */
clp_list_destroy(&files);
clp_kv_destroy(&env);
```

Both init functions return `CLP_OK` or `CLP_ERR_ALLOC`. `clp.h` declares `d_unordered_map_t`, so KV users only need `d_unordered_map.h` to read the map (`d_unordered_map_get`).

Stored `d_string_view_t`s point into `argv` and stay valid as long as `argv` does.

---

## Quick start

```c
#include "clp.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    (void) argc;

    bool            verbose = false; /* default */
    long            jobs    = 1;     /* default */
    d_string_view_t file;
    clp_error_t     err;
    clp_command_t  *root;
    clp_option_t   *verbose_opt, *jobs_opt;
    clp_operand_t  *file_op;

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

    clp_command_t *cmd = NULL;
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

The snippets below omit error checks on the builders for brevity; every builder returns a `clp_status_t` you should check.

### Subcommands

```c
clp_command_t *root, *add, *rm;
clp_new_command(&root, 0, "prog", "description", NULL, NULL, &err);
clp_new_command(&add, 1, "add", "add a file", NULL, NULL, &err);
clp_new_command(&rm, 2, "rm", "remove a file", NULL, NULL, &err);
clp_add_command_sub_command(root, add, &err);
clp_add_command_sub_command(root, rm, &err);

clp_command_t *cmd = NULL;
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
usize         debug = 0; /* counting starts from the initial value */
clp_option_t *opt;
clp_new_option_count(&opt, "debug", "d", "increase debug level", &debug, false, &err);
clp_add_command_option(root, opt, &err);
/* after parse: debug == number of -d flags */
```

### List option (`--files a,b,c`)

```c
d_dyn_array_t files;
clp_list_init(&files, &err);
clp_new_option_list(&opt, "files", "f", "input files", &files, false, false, &err);
clp_add_command_option(root, opt, &err);
/* after parse: files holds one d_string_view_t per item */
```

### Key=value option (`--env HOST=localhost,PORT=8080`)

```c
d_unordered_map_t env;
clp_kv_init(&env, &err);
clp_new_option_kv(&opt, "env", "e", "environment variables", &env, false, false, &err);
clp_add_command_option(root, opt, &err);

/* lookup after parse */
d_string_view_t  key = D_STRING_VIEW_FROM_LITERAL("HOST");
d_string_view_t *val = d_unordered_map_get(&env, &key);
```

### List operand (consumes all remaining positional args)

```c
d_dyn_array_t  files;
clp_operand_t *op;
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
d_string_view_t token;
clp_new_option(&opt, "token", "t", "API token", &token, true, /*global=*/true, &err);
clp_add_command_option(root, opt, &err);
/* prog --token abc push  and  prog push --token abc  both set token */
```

---

## Input checkers

```c
typedef const char *(*clp_checker_t)(const void *value);
```

A checker runs on a value right after it has been converted, **before** it is written to `dest`. Return `NULL` to accept it, or a message with static lifetime to reject it: parsing then fails with `CLP_ERR_BAD_VALUE`, the message is appended to the usual `invalid value '...' for '--x'` error, and `dest` keeps its previous value.

What `value` points to:

- **SET**: the converted value, with the same type as `dest` (`const long *` for a `long` dest, ...). It is a scratch copy, not `dest` itself.
- **LIST**: one item, as a `const d_string_view_t *`, including each comma-separated piece of an option value.
- **KV**: one pair, as a `const clp_key_value_t *` (`{ d_string_view_t key; d_string_view_t value; }`; the views are not NUL-terminated). Malformed pairs are rejected with `CLP_ERR_BAD_KV` first.
- Never run for bool flags or count options.

```c
static const char *jobs_range(const void *p)
{
    long jobs = *(const long *) p;
    return (jobs >= 1 && jobs <= 64) ? NULL : "must be in [1 64]";
}

static const char *check_env(const void *p)
{
    const clp_key_value_t *kv = p;
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
typedef const char *(*clp_transform_t)(void *ctx, const char *arg);
```

- `arg` is the raw, NUL-terminated argument, pointing into `argv`.
- `ctx` is passed through untouched (it may be `NULL`). Because the target comes in through `ctx`, one transform can serve several options, with no global state.
- Return `NULL` to accept, or a message with static lifetime to reject: parsing fails with `CLP_ERR_BAD_VALUE` and `value_set` stays `false`. Anything the transform stored before rejecting is its own responsibility.
- clp only tracks `value_set` and `required`; it stores nothing.
- SET only, no checker (the transform validates itself). Shown as `<value>` in `--help`.

```c
typedef enum { TS_NONE, TS_ONLY, TS_ADDR } ts_type_t;

static const char *parse_timestamp(void *ctx, const char *arg)
{
    ts_type_t *out = ctx;
    if (strcmp(arg, "tsonly") == 0)
        *out = TS_ONLY;
    else if (strcmp(arg, "tsaddr") == 0)
        *out = TS_ADDR;
    else
        return "expected \"tsonly\" or \"tsaddr\"";
    return NULL;
}

ts_type_t ip_ts  = TS_NONE; /* defaults are the initial values, as with dest */
ts_type_t tcp_ts = TS_NONE;
/* one transform, two options: each result goes to its own ctx */
clp_new_option_transform(&opt, "ip-timestamp", NULL, "IP timestamp of type FLAG", parse_timestamp, &ip_ts, false, false, &err);
clp_new_option_transform(&opt2, "tcp-timestamp", NULL, "TCP timestamp of type FLAG", parse_timestamp, &tcp_ts, false, false, &err);
clp_new_operand_transform(&op, "mode", "run mode", parse_mode, &mode, true, &err);
```

---

## Command validators (post-parse)

Checkers and transforms see one value at a time. Rules that span several arguments ("`-f` and `-i` are incompatible", "`--tsaddr` needs `--timestamp`") go in a validator, given to `clp_new_command` with a `ctx`:

```c
typedef const char *(*clp_validator_t)(void *ctx);
```

- Runs once the command's arguments are all parsed and its required arguments are present. It does not run if parsing failed earlier, and it runs even when the command got no arguments.
- Only the validator of the command parsing ends on runs (the one returned through `command`), not its ancestors'.
- `ctx` is passed through untouched.
- Return `NULL` to accept, or a message with static lifetime to reject: parsing fails with `CLP_ERR_VALIDATOR`, `err->command` is the command and the message reads `command <name>: <your message>`.

```c
typedef struct { clp_option_t *flood, *interval; } ping_rules_t;

static const char *check_ping(void *ctx)
{
    ping_rules_t *r = ctx;
    if (r->flood->value_set && r->interval->value_set)
        return "-f and -i are incompatible";
    return NULL;
}

ping_rules_t rules;
clp_new_command(&root, 0, "ping", "send ICMP ECHO_REQUEST", check_ping, &rules, &err);
clp_new_option(&rules.flood, "flood", "f", "flood ping", &flood, false, false, &err);
clp_new_option(&rules.interval, "interval", "i", "wait between packets", &interval, false, false, &err);
```

---

## API reference

### Lifecycle

```c
/* Builders never exit: they return a clp_status_t and fill err (may be NULL).
   Constructors set *out to NULL on failure. */
clp_status_t clp_new_command(clp_command_t **out, int code, char *name, char *description, clp_validator_t validator, void *ctx, clp_error_t *err);
clp_status_t clp_add_command_option(clp_command_t *command, clp_option_t *option, clp_error_t *err);
clp_status_t clp_add_command_operand(clp_command_t *command, clp_operand_t *operand, clp_error_t *err);
clp_status_t clp_add_command_sub_command(clp_command_t *command, clp_command_t *sub_command, clp_error_t *err);

clp_status_t clp_try_parse_args(clp_command_t *root, char **argv, clp_command_t **command, clp_error_t *err); /* never exits */
void         clp_parse_args(clp_command_t *root, char **argv, clp_command_t **command);                   /* exits on error */
void         clp_print_error(FILE *stream, const clp_error_t *err);
const char  *clp_status_to_str(clp_status_t code);

void clp_cleanup(clp_command_t *root);         /* frees the tree and everything added to it; never touches dests */
void clp_free_option(clp_option_t *option);    /* only for an option not added to a command (NULL is a no-op) */
void clp_free_operand(clp_operand_t *operand); /* only for an operand not added to a command (NULL is a no-op) */

/* LIST / KV collections (caller-owned) */
clp_status_t clp_list_init(d_dyn_array_t *list, clp_error_t *err);    /* CLP_OK or CLP_ERR_ALLOC */
clp_status_t clp_kv_init(d_unordered_map_t *map, clp_error_t *err);   /* CLP_OK or CLP_ERR_ALLOC */
void         clp_list_destroy(d_dyn_array_t *list);
void         clp_kv_destroy(d_unordered_map_t *map);
```

Ownership: an option/operand belongs to the command once `clp_add_command_*` succeeds, and `clp_cleanup` frees it. If the add fails it is still yours — release it with `clp_free_option` / `clp_free_operand`.

### Lookup

```c
clp_option_t  *clp_get_option_by_short(clp_command_t *command, char shrt);
clp_option_t  *clp_get_option_by_long (clp_command_t *command, d_string_view_t lng);
clp_operand_t *clp_get_operand        (clp_command_t *command, d_string_view_t name);
```

### Option macros

Every macro takes `out` first and `err` last, and returns a `clp_status_t`.

```c
clp_new_option          (out, ln, sn, desc, dest, required, global, err)          /* SET; type from dest */
clp_new_option_count    (out, ln, sn, desc, dest, global, err)                    /* dest: usize * */
clp_new_option_list     (out, ln, sn, desc, dest, required, global, err)          /* dest: d_dyn_array_t * */
clp_new_option_kv       (out, ln, sn, desc, dest, required, global, err)          /* dest: d_unordered_map_t * */

clp_new_option_checked     (out, ln, sn, desc, dest, required, global, checker, err)
clp_new_option_list_checked(out, ln, sn, desc, dest, required, global, checker, err)
clp_new_option_kv_checked  (out, ln, sn, desc, dest, required, global, checker, err)

clp_new_option_transform(out, ln, sn, desc, transform, ctx, required, global, err) /* no dest; ctx → transform */
```

### Operand macros

```c
clp_new_operand     (out, name, desc, dest, required, err)                        /* SET; type from dest */
clp_new_operand_list(out, name, desc, dest, required, err)                        /* dest: d_dyn_array_t * */
clp_new_operand_kv  (out, name, desc, dest, required, err)                        /* dest: d_unordered_map_t * */

clp_new_operand_checked     (out, name, desc, dest, required, checker, err)
clp_new_operand_list_checked(out, name, desc, dest, required, checker, err)
clp_new_operand_kv_checked  (out, name, desc, dest, required, checker, err)

clp_new_operand_transform(out, name, desc, transform, ctx, required, err)         /* no dest; ctx → transform */
```

### Inserting a table of options / operands

`clp_insert_opts` and `clp_insert_operands` create and register a whole array in one call. Each `clp_option_info_t` / `clp_operand_info_t` holds the arguments of `clp_new_option_raw` / `clp_new_operand_raw`, with the same rules (and, like the raw constructors, no check that `dest` matches `type`).

```c
typedef struct clp_option_info_t {
    char *long_name; char *short_name; char *description; void *dest;
    clp_opt_action_t action; clp_type_t type; bool required; bool global;
    clp_checker_t checker; clp_transform_t transform;
} clp_option_info_t;

typedef struct clp_operand_info_t {
    char *name; char *description; void *dest;
    clp_operand_action_t action; clp_type_t type; bool required;
    clp_checker_t checker; clp_transform_t transform;
} clp_operand_info_t;

clp_status_t clp_insert_opts(clp_command_t *command, const clp_option_info_t *infos, usize count, clp_error_t *err);
clp_status_t clp_insert_operands(clp_command_t *command, const clp_operand_info_t *infos, usize count, clp_error_t *err);
```

```c
clp_option_info_t opts[] = {
    {"verbose", "v", "verbose output", &verbose, CLP_OPT_ACT_SET, CLP_TYPE_BOOL, false, false, NULL, NULL},
    {"jobs",    "j", "worker count",   &jobs,    CLP_OPT_ACT_SET, CLP_TYPE_LONG, false, false, jobs_range, NULL},
};
clp_operand_info_t ops[] = {
    {"host", "host to reach", &host, CLP_OPERAND_ACT_SET, CLP_TYPE_STR, true, NULL, NULL},
};
if (clp_insert_opts(root, opts, sizeof(opts) / sizeof(*opts), &err) != CLP_OK ||
    clp_insert_operands(root, ops, sizeof(ops) / sizeof(*ops), &err) != CLP_OK)
    ...
```

Entries are inserted in order and insertion stops at the first failure: `err` is filled and its status returned, the entry that failed is released, and the entries inserted before it stay on the command (freed by `clp_cleanup`). `infos` may be `NULL` when `count` is `0`.

### Raw constructors (full control)

The macros cover every combination; the raw constructors do not check that `dest` matches `type`, so prefer the macros.

```c
clp_status_t clp_new_option_raw(clp_option_t **out, char *long_name, char *short_name, char *description,
                             void *dest, clp_opt_action_t action, clp_type_t type, bool required, bool global,
                             clp_checker_t checker, clp_transform_t transform, clp_error_t *err);

clp_status_t clp_new_operand_raw(clp_operand_t **out, char *name, char *description,
                              void *dest, clp_operand_action_t action, clp_type_t type, bool required,
                              clp_checker_t checker, clp_transform_t transform, clp_error_t *err);
```

A `transform` requires `CLP_TYPE_CUSTOM` (and `CLP_TYPE_CUSTOM` requires a transform), the SET action, and no checker; `dest` is then passed to the transform as its `ctx` and may be `NULL`. Any other combination returns `CLP_ERR_INVALID_TYPE`.

---

## Errors

Builders and `clp_try_parse_args` never exit: they return a `clp_status_t` and fill `clp_error_t` (print it with `clp_print_error`). `clp_parse_args` prints the same message to **stderr** and calls `exit(EXIT_FAILURE)` instead. Passing `NULL` where a parameter must not be `NULL` is a programming error caught by `assert`.

### At registration (builders)

| Condition                                                          | Status                                 |
| ------------------------------------------------------------------ | -------------------------------------- |
| Long option name starts with `-` or `_`, or contains invalid chars | `CLP_ERR_INVALID_NAME`                 |
| Short option name is not alphanumeric or `?`                       | `CLP_ERR_INVALID_NAME`                 |
| Long option name is `"help"` (reserved)                            | `CLP_ERR_RESERVED_NAME`                |
| `CLP_OPT_ACT_COUNT` paired with a type other than `CLP_TYPE_USIZE`         | `CLP_ERR_INVALID_TYPE`                 |
| Transform / `CLP_TYPE_CUSTOM` / checker combination not allowed        | `CLP_ERR_INVALID_TYPE`                 |
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
