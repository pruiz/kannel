/*
 *
 * wsopt.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Optimizations for the WMLScript symbolic assembler.
 *
 */

#ifndef WSOPT_H
#define WSOPT_H

/********************* Optimizations ************************************/

/* Optimize the symbolic assembler in the compiler `compiler'.  The
   enabled optimizations can be found from the compiler's options.
   The possible errors are reported through the error variables of the
   `compiler'. */
void ws_asm_optimize(WsCompilerPtr compiler);

#endif /* WSOPT_H */
