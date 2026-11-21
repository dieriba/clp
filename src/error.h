#ifndef ERROR_H
#define ERROR_H

#include "clp.h"

/* Fill err (if not NULL) with code, command and a printf-formatted message,
 * then return code so call sites can write `return error_fail(...)`. */
ClpStatus error_fail(ClpError *err, ClpStatus code, Command *command, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/* Append printf-formatted text to err->message (no-op if err is NULL).
 * Silently truncates once the buffer is full. */
void error_append(ClpError *err, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Evaluate expr; if it is not CLP_OK, return its status from the current function. */
#define CLP_TRY(expr)                                                                                                                                                                                  \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        ClpStatus clp_try_status__ = (expr);                                                                                                                                                           \
        if (clp_try_status__ != CLP_OK)                                                                                                                                                                \
            return clp_try_status__;                                                                                                                                                                   \
    } while (0)

#endif
