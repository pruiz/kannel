/*
 * ota_compiler.h: The interface to ota settings and bookmarks tokenizer 
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#ifndef OTA_COMPILER_H
#define OTA_COMPILER_H

#include "gwlib/gwlib.h"

/*
 * Compiles an ota document to ota binary. Input textual form of a ota document
 * and its charset (from http headers), output the document in a tokenised 
 * form.
 * Returns -1 when error, 0 otherwise.
 */

int ota_compile(Octstr *ota_doc, Octstr *charset, Octstr **ota_binary);

#endif
