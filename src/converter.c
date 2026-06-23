#include "converter.h"

#include <errno.h>
#define TRUE_STR "true"
#define FALSE_STR "false"

char *s_to_usize(const char *s, Value *value)
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

char *s_to_long(const char *s, Value *value)
{
    char *endptr;
    errno              = 0;
    value->value_usize = strtoll(s, &endptr, 10);

    if (errno == ERANGE)
        return "value too high/low ";
    else if (endptr == s || *endptr != 0)
        return "unexpected digit";

    return NULL;
}

char *s_to_double(const char *s, Value *value)
{
    char *endptr;
    value->value_double = strtod(s, &endptr);

    if (errno == ERANGE)
        return "value too high/low ";
    else if (endptr == s || *endptr != 0)
        return "unexpected digit";
    return NULL;
}

char *s_to_char(const char *s, Value *value)
{
    char c = s[0];
    if (c == 0)
        return "cannot parse char from empty string";
    if (s[1] != 0)
        return "too many characters in string";
    value->value_char = c;
    return NULL;
}

char *s_to_string(const char *s, Value *value)
{
    value->value_d_string_view = d_string_view_from_c_string(s);
    return NULL;
}

char *s_to_bool(const char *s, Value *value)
{
    if (PSEUDO_FAST_STRCMP(s, TRUE_STR))
        value->value_bool = true;
    else if (PSEUDO_FAST_STRCMP(s, FALSE_STR))
        value->value_bool = false;
    else
        return "expected string to be \"true\" or \"false\"";
    return NULL;
}

const char *type_to_str(Type type)
{
    switch (type)
    {
    case TYPE_LONG:
        return "long";
    case TYPE_BOOL:
        return "bool";
    case TYPE_USIZE:
        return "usize";
    case TYPE_STR:
        return "str";
    case TYPE_CHAR:
        return "char";
    case TYPE_DOUBLE:
        return "double";
    default:
        break;
    }
    return "UNKNOWN TYPE";
}

ConversionFn type_to_conversion_fn(Type type)
{
    switch (type)
    {
    case TYPE_USIZE:
        return s_to_usize;
    case TYPE_LONG:
        return s_to_long;
    case TYPE_STR:
        return s_to_string;
    case TYPE_CHAR:
        return s_to_char;
    case TYPE_DOUBLE:
        return s_to_double;
    case TYPE_BOOL:
        return s_to_bool;
    default:
        break;
    }
    return NULL;
}