/*
 *
 * wsstdlib.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Standard libraries.
 *
 */

#ifndef WSSTDLIB_H
#define WSSTDLIB_H

/********************* Prototypes for global functions ******************/

/* Returns the library and function indexes and the exact amount of
   arguments for the function `function' of the library `library'.
   The function returns WS_TRUE if the operation was successful.  If
   the operation failed the function returns WS_FALSE and it sets the
   `{l,f}index_found_return' to WS_FALSE to indicate whether the
   library or the function name was unknown.  Note that if the library
   is unknown, then also the function is unknown. */
WsBool ws_stdlib_function(const char *library, const char *function,
			  WsUInt16 *lindex_return, WsUInt8 *findex_return,
			  WsUInt8 *num_args_return,
			  WsBool *lindex_found_return,
			  WsBool *findex_found_return);

/* Returns the library and function names, corresponding to their
   indexes `lindex' and `findex'.  The function returns WS_TRUE if
   both the library and function name could be resolved.  Otherwise
   the function returns WS_FALSE and sets the failed name pointer(s)
   to NULL. */
WsBool ws_stdlib_function_name(WsUInt16 lindex, WsUInt8 findex,
			       const char **library_return,
			       const char **function_return);

#endif /* not WSSTDLIB_H */
