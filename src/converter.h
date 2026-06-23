#ifndef CONVERTER_H
#define CONVERTER_H
#include "clp.h"
typedef char *(*ConversionFn)(const char *to_convert, Value *value);

char        *s_to_bool(const char *s, Value *value);
char        *s_to_string(const char *s, Value *value);
char        *s_to_char(const char *s, Value *value);
char        *s_to_double(const char *s, Value *value);
char        *s_to_long(const char *s, Value *value);
char        *s_to_usize(const char *s, Value *value);
ConversionFn type_to_conversion_fn(Type type);
const char  *type_to_str(Type type);
#endif