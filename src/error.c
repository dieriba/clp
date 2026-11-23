#include "error.h"

#include "print.h"

#include <stdarg.h>
#include <string.h>

static void vappend(ClpError *err, const char *fmt, va_list ap)
{
    usize len = strlen(err->message);
    if (len + 1 >= sizeof(err->message))
        return;
    vsnprintf(err->message + len, sizeof(err->message) - len, fmt, ap);
}

ClpStatus error_fail(ClpError *err, ClpStatus code, Command *command, const char *fmt, ...)
{
    if (err == NULL)
        return code;
    err->code       = code;
    err->command    = command;
    err->message[0] = '\0';

    va_list ap;
    va_start(ap, fmt);
    vappend(err, fmt, ap);
    va_end(ap);
    return code;
}

void error_append(ClpError *err, const char *fmt, ...)
{
    if (err == NULL)
        return;
    va_list ap;
    va_start(ap, fmt);
    vappend(err, fmt, ap);
    va_end(ap);
}

void clp_print_error(FILE *stream, const ClpError *err)
{
    if (err->code == CLP_OK || err->code == CLP_HELP_SHOWN)
        return;
    fprintf(stream, "clp: %s", err->message);
    if (err->code == CLP_ERR_MISSING_REQUIRED && err->command != NULL)
    {
        print_usage_line(stream, err->command);
        fprintf(stream, "For more information, try '--help'.\n");
    }
}

const char *clp_status_to_str(ClpStatus code)
{
    switch (code)
    {
    case CLP_OK:
        return "ok";
    case CLP_HELP_SHOWN:
        return "help shown";
    case CLP_ERR_ALLOC:
        return "allocation failure";
    case CLP_ERR_INVALID_NAME:
        return "invalid name";
    case CLP_ERR_UNKNOWN_OPTION:
        return "unknown option";
    case CLP_ERR_MISSING_ARG:
        return "missing argument";
    case CLP_ERR_UNEXPECTED_ARG:
        return "unexpected argument";
    case CLP_ERR_BAD_VALUE:
        return "invalid value";
    case CLP_ERR_BAD_KV:
        return "invalid key=value pair";
    case CLP_ERR_TOO_MANY_OPERANDS:
        return "too many operands";
    case CLP_ERR_MISSING_REQUIRED:
        return "missing required argument";
    case CLP_ERR_DUPLICATE:
        return "duplicate option";
    case CLP_ERR_RESERVED_NAME:
        return "reserved name";
    case CLP_ERR_INVALID_TYPE:
        return "invalid type for action";
    case CLP_ERR_CONFLICT:
        return "operands and subcommands conflict";
    case CLP_ERR_OPERAND_ORDER:
        return "required operand after optional operand";
    }
    return "unknown status";
}
