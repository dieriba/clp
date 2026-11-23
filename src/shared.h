#ifndef SHARED_H
#define SHARED_H
#include <stdio.h>

#define HYPHEN "-"
#define DOUBLE_HYPHEN HYPHEN HYPHEN
#define clp_eprint(...) eprint(clp, __VA_ARGS__)
#define clp_eprint_exit(...) eprint_exit(clp, __VA_ARGS__)
#define clp_exit_out_of_memory() clp_eprint_exit("out of memory\n");
#define clp_invalid_arg_exit(...) clp_eprint_exit("invalid argument: " __VA_ARGS__)
#define STR_STARTS_WITH_HYPEN(s) *(s) == '-'
#define HELP_OPT "help"
#define FLAG_SHORT_OPT_NOT_SET 0xFF

#ifdef WARNING
#define clp_print_warning(...)                                                                                                                                                                         \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        fprintf(stdout, __VA_ARGS__);                                                                                                                                                                   \
    } while (0)
#else
#define clp_print_warning(...)                                                                                                                                                                         \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
    } while (0)
#endif

#endif