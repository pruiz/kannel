/*
 * wap_push_pap_compiler.h - compiling PAP control document to a Kannel event.
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#ifndef WAP_PUSH_PAP_COMPILER_H
#define WAP_PUSH_PAP_COMPILER_H 

#include "wap/wap_events.h"
#include "gwlib/gwlib.h"

/*
 * Compiler PAP control document to a corresponding Kannel event. Checks vali-
 * dity of the document. The caller must initialize wap event to NULL. In add-
 * ition, it must free memory allocated by this function. 
 * Returs 0 when OK, -1 when error.
 */
int pap_compile(Octstr *pap_content, WAPEvent **e);

#endif
