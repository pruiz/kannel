/*
 *
 * wserror.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * Error and information reporting functions.
 *
 */

#ifndef WSERROR_H
#define WSERROR_H

/********************* High-level functions *****************************/

/* Report an informative message `message'. */
void ws_info(WsCompilerPtr compiler, char *message, ...);

/* Report a fatal (non-recovable) error and terminate the program
   brutally.  This is only used to report internal inconsistencies and
   bugs.  This will never return. */
void ws_fatal(char *fmt, ...);

/* Report an out-of-memory error. */
void ws_error_memory(WsCompilerPtr compiler);

/* Report a syntax error from the line `line' of the current input
   stream.  If the argument `line' is 0, the error line number is the
   current line number of the input stream. */
void ws_error_syntax(WsCompilerPtr compiler, WsUInt32 line);

/* Report a source stream specific (WMLScript language specific) error
   `message' from the source stream line number `line'.  If the
   argument `line' is 0, the line number information is taken from the
   input stream's current position. */
void ws_src_error(WsCompilerPtr compiler, WsUInt32 line, char *message, ...);

/* Report a source stream specific warning `message' from the source
   stram line number `line'.  If the argument `line' is 0, the line
   number information is taken from the input stream's current
   position. */
void ws_src_warning(WsCompilerPtr compiler, WsUInt32 line, char *message, ...);


/********************* Low-level functions ******************************/

/* Standard output and error streams.  These are handy macros to fetch
   the I/O function and its context corresponding to the stream from
   the compiler. */

#define WS_STREAM(_stream)		\
  compiler->params._stream ## _cb,	\
  compiler->params._stream ## _cb_context

#define WS_STDOUT WS_STREAM(stdout)
#define WS_STDERR WS_STREAM(stderr)

/* Print the message `fmt', `...' to the stream `io', `context'.  Note
   that not all format and format specifiers of the normal printf()
   are supported. */
void ws_fprintf(WsIOProc io, void *context, const char *fmt, ...);

/* Print the message `fmt', `ap' to the stream `io', `context'. */
void ws_vfprintf(WsIOProc io, void *context, const char *fmt, va_list ap);

/* Print the string `str' to the stream `io', `context'.  The function
   will not print newline after the string. */
void ws_puts(WsIOProc io, void *context, const char *str);

/* Print the character `ch' to the stream `io', `context'. */
void ws_fputc(int ch, WsIOProc io, void *context);

#endif /* not WSERROR_H */
