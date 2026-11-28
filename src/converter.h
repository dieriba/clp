#ifndef CONVERTER_H
#define CONVERTER_H
#include "clp.h"
typedef char *(*conversion_fn_t)(const char *to_convert, clp_value_t *value);

char           *s_to_bool(const char *s, clp_value_t *value);
char           *s_to_string(const char *s, clp_value_t *value);
char           *s_to_char(const char *s, clp_value_t *value);
char           *s_to_double(const char *s, clp_value_t *value);
char           *s_to_long(const char *s, clp_value_t *value);
char           *s_to_usize(const char *s, clp_value_t *value);
conversion_fn_t type_to_conversion_fn(clp_type_t type);
const char     *type_to_str(clp_type_t type);
#endif