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
 * Possible address types
 */
enum {
    ADDR_IPV4 = 0,
    ADDR_PLMN = 1,
    ADDR_USER = 2,
    ADDR_IPV6 = 3,
    ADDR_WINA = 4
};

/*
 *Compile PAP control document to a corresponding Kannel event. Checks vali-
 * dity of the document. The caller must initialize wap event to NULL. In add-
 * ition, it must free memory allocated by this function. 
 *
 * After compiling, some semantic analysing of the resulted event. 
 *
 * Note that entities in the DTD are parameter entities and they can appear 
 * only in DTD (See site http://www.w3.org/TR/REC-xml, Chapter 4.1). So we do 
 * not need to worry about them in the document itself.
 *
 * Returns 0, when success
 *        -1, when a non-implemented pap feature is asked for
 *        -2, when error
 * In addition, returns a newly created wap event corresponding the pap 
 * control message, if success, wap event NULL otherwise.
 */
int pap_compile(Octstr *pap_content, WAPEvent **e);

int parse_address(Octstr **attr_value, long *type_of_address);

#endif

