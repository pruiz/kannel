/*
 * wap_push_sl_compiler.h: The interface to sl tokenizer 
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#ifndef WAP_PUSH_SL_COMPILER_H
#define WAP_PUSH_SL_COMPILER_H

#include "gwlib/gwlib.h"

/*
 * Compiles a sl document to sl binary. Input textual form of a sl document
 * and its charset (from http headers), output the document in a tokenised 
 * form.
 */

int sl_compile(Octstr *sl_doc, Octstr *charset, Octstr **sl_binary);

#endif
