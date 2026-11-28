#include "converter.h"

#include "d_general_lib.h"

#include <errno.h>
#define TRUE_STR "true"
#define FALSE_STR "false"

char *s_to_usize(const char *s, clp_value_t *value)
{
    char *endptr;
    errno              = 0;
    value->value_usize = strtoull(s, &endptr, 10);

    if (errno == ERANGE)
        return "number to high";
    else if (endptr == s || *endptr != 0)
        return "unexpected digit";

    return NULL;
}

char *s_to_long(const char *s, clp_value_t *value)
{
    char *endptr;
    errno             = 0;
    value->value_long = strtol(s, &endptr, 10);

    if (errno == ERANGE)
        return "value too high/low ";
    else if (endptr == s || *endptr != 0)
        return "unexpected digit";

    return NULL;
}

char *s_to_double(const char *s, clp_value_t *value)
{
    char *endptr;
    value->value_double = strtod(s, &endptr);

    if (errno == ERANGE)
        return "value too high/low ";
    else if (endptr == s || *endptr != 0)
        return "unexpected digit";
    return NULL;
}

char *s_to_char(const char *s, clp_value_t *value)
{
    char c = s[0];
    if (c == 0)
        return "cannot parse char from empty string";
    if (s[1] != 0)
        return "too many characters in string";
    value->value_char = c;
    return NULL;
}

char *s_to_string(const char *s, clp_value_t *value)
{
    value->value_d_string_view = d_string_view_from_c_string(s);
    return NULL;
}

char *s_to_bool(const char *s, clp_value_t *value)
{
    if (PSEUDO_FAST_STRCMP(s, TRUE_STR))
        value->value_bool = true;
    else if (PSEUDO_FAST_STRCMP(s, FALSE_STR))
        value->value_bool = false;
    else
        return "expected string to be \"true\" or \"false\"";
    return NULL;
}

const char *type_to_str(clp_type_t type)
{
    switch (type)
    {
    case CLP_TYPE_LONG:
        return "long";
    case CLP_TYPE_BOOL:
        return "bool";
    case CLP_TYPE_USIZE:
        return "usize";
    case CLP_TYPE_STR:
        return "str";
    case CLP_TYPE_CHAR:
        return "char";
    case CLP_TYPE_DOUBLE:
        return "double";
    case CLP_TYPE_CUSTOM:
        return "value";
    default:
        break;
    }
    return "UNKNOWN TYPE";
}

conversion_fn_t type_to_conversion_fn(clp_type_t type)
{
    switch (type)
    {
    case CLP_TYPE_USIZE:
        return s_to_usize;
    case CLP_TYPE_LONG:
        return s_to_long;
    case CLP_TYPE_STR:
        return s_to_string;
    case CLP_TYPE_CHAR:
        return s_to_char;
    case CLP_TYPE_DOUBLE:
        return s_to_double;
    case CLP_TYPE_BOOL:
        return s_to_bool;
    default:
        break;
    }
    return NULL;
}