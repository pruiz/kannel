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
 * Compile PAP control document to a corresponding Kannel event. Checks vali-
 * dity of the document. The caller must initialize wap event to NULL. In add-
 * ition, it must free memory allocated by this function. 
 *
 * After compiling, some semantic analysing of the resulted event. Do not
 * accept an IP address, when a non-IP bearer is requested, and a phone number,
 * when an IP-bearer is requested.
 *
 * Returns 0, when success
 *        -1, when a non-implemented pap feature is asked for
 *        -2, when error
 * In addition, returns a newly created wap event corresponding the pap 
 * control message, if success, wap event NULL otherwise.
 */
int pap_compile(Octstr *pap_content, WAPEvent **e);

#endif
