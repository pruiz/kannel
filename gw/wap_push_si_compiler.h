/*
 * wap_push_si_compiler.h: The interface to si tokenizer 
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#ifndef WAP_PUSH_SI_COMPILER_H
#define WAP_PUSH_SI_COMPILER_H

#include "gwlib/gwlib.h"

/*
 * Compiles a si document to si binary. Input textual form of a si document
 * and its charset (from http headers), output the document in a tokenised 
 * form.
 */

int si_compile(Octstr *si_doc, Octstr *charset, Octstr **si_binary);

#endif
