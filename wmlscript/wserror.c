/*
 *
 * wserror.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Error and information reporting functions.
 *
 */

#include "wsint.h"

/********************* High-level functions *****************************/

void ws_info(WsCompilerPtr compiler, char *message, ...)
{
    va_list ap;

    if (!compiler->params.verbose)
        return;

    ws_puts(WS_STDOUT, "wsc: ");

    va_start(ap, message);
    ws_vfprintf(WS_STDOUT, message, ap);
    va_end(ap);

    ws_puts(WS_STDOUT, WS_LINE_TERMINATOR);
}


void ws_fatal(char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "wsc: fatal: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");

    abort();
}


void ws_error_memory(WsCompilerPtr compiler)
{
    gw_assert(compiler->magic = COMPILER_MAGIC);

    if (compiler->errors & WS_ERROR_B_MEMORY)
        /* We have already reported this error. */
        return;

    compiler->errors |= WS_ERROR_B_MEMORY;
    ws_puts(WS_STDERR, "wsc: error: out of memory" WS_LINE_TERMINATOR);
}


void ws_error_syntax(WsCompilerPtr compiler, WsUInt32 line)
{
    gw_assert(compiler->magic = COMPILER_MAGIC);

    if (compiler->errors & WS_ERROR_B_MEMORY)
        /* It makes no sense to report syntax errors when we have run out
           of memory.  This information is not too valid. */ 
        return;

    if (line == 0)
        line = compiler->linenum;

    if (compiler->last_syntax_error_line == line)
        /* It makes no sense to report multiple syntax errors from the
           same line. */ 
        return;

    compiler->last_syntax_error_line = line;
    compiler->errors |= WS_ERROR_B_SYNTAX;

    ws_fprintf(WS_STDERR, "%s:%u: syntax error" WS_LINE_TERMINATOR,
               compiler->input_name, line);
}


void ws_src_error(WsCompilerPtr compiler, WsUInt32 line, char *message, ...)
{
    va_list ap;

    gw_assert(compiler->magic = COMPILER_MAGIC);

    if (line == 0)
        line = compiler->linenum;

    compiler->errors |= WS_ERROR_B_SEMANTIC;

    ws_fprintf(WS_STDERR, "%s:%u: ", compiler->input_name, line);

    va_start(ap, message);
    ws_vfprintf(WS_STDERR, message, ap);
    va_end(ap);

    ws_puts(WS_STDERR, WS_LINE_TERMINATOR);

    compiler->num_errors++;
}


void ws_src_warning(WsCompilerPtr compiler, WsUInt32 line, char *message, ...)
{
    va_list ap;

    gw_assert(compiler->magic = COMPILER_MAGIC);

    if (line == 0)
        line = compiler->linenum;

    ws_fprintf(WS_STDERR, "%s:%u: warning: ", compiler->input_name, line);

    va_start(ap, message);
    ws_vfprintf(WS_STDERR, message, ap);
    va_end(ap);

    ws_puts(WS_STDERR, WS_LINE_TERMINATOR);

    compiler->num_errors++;
}

/********************* Low-level functions ******************************/

void ws_fprintf(WsIOProc io, void *context, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    ws_vfprintf(io, context, fmt, ap);
    va_end(ap);
}


void ws_vfprintf(WsIOProc io, void *context, const char *fmt, va_list ap)
{
    int start, i;

    for (start = 0, i = 0; fmt[i]; i++)
        if (fmt[i] == '%' && fmt[i + 1]) {
            char buf[256];
            char *cp;
            int ival;
            unsigned int uival;
            int padder = ' ';
            int left = 0;
            unsigned int width = 0;

            if (fmt[i + 1] == '%') {
                /* An escaped `%'.  Print leading data including the `%'
                          character. */
                i++;
                (*io)(fmt + start, i - start, context);
                start = i + 1;
                continue;
            }

            /* An escape sequence. */

            /* Print leading data if any. */
            if (i > start)
                (*io)(fmt + start, i - start, context);

            /* We support a minor sub-set of the printf()'s formatting
                      capabilities.  Let's see what we got. */
            i++;

            /* Alignment? */
            if (fmt[i] == '-') {
                left = 1;
                i++;
            }

            /* Padding? */
            if (fmt[i] == '0') {
                padder = '0';
                i++;
            }

            /* Width? */
            while ('0' <= fmt[i] && fmt[i] <= '9') {
                width *= 10;
                width += fmt[i++] - '0';
            }

            /* Check the format. */
            cp = buf;
            switch (fmt[i]) {
            case 'c': 		/* character */
                ival = (int) va_arg(ap, int);

                snprintf(buf, sizeof(buf), "%c", (char) ival);
                cp = buf;
                break;

            case 's': 		/* string */
                cp = va_arg(ap, char *);
                break;

            case 'd': 		/* integer */
                ival = va_arg(ap, int);

                snprintf(buf, sizeof(buf), "%d", ival);
                cp = buf;
                break;

            case 'u': 		/* unsigned integer */
                uival = va_arg(ap, unsigned int);

                snprintf(buf, sizeof(buf), "%u", uival);
                cp = buf;
                break;

            case 'x': 		/* unsigned integer in hexadecimal format */
                uival = va_arg(ap, unsigned int);

                snprintf(buf, sizeof(buf), "%x", uival);
                cp = buf;
                break;

            default:
                ws_fatal("ws_vfprintf(): format %%%c not implemented", fmt[i]);
                break;
            }

            if (left)
                /* Output the value left-justified. */
                (*io)(cp, strlen(cp), context);

            /* Need padding? */
            if (width > strlen(cp)) {
                /* Yes we need. */
                int amount = width - strlen(cp);

                while (amount-- > 0)
                    ws_fputc(padder, io, context);
            }

            if (!left)
                /* Output the value right-justified. */
                (*io)(cp, strlen(cp), context);

            /* Process more. */
            start = i + 1;
        }

    /* Print trailing data if any. */
    if (i > start)
        (*io)(fmt + start, i - start, context);
}


void ws_puts(WsIOProc io, void *context, const char *str)
{
    (*io)(str, strlen(str), context);
}


void ws_fputc(int ch, WsIOProc io, void *context)
{
    char c = (char) ch;

    (*io)(&c, 1, context);
}
