/*
 * wsp.h - WSP PDU headers implementation header
 *
 * Kalle Marjola <rpr@wapit.com>
 */

#ifndef WSP_HEADERS_H
#define WSP_HEADERS_H

#include "gwlib/gwlib.h"
#include "wsp.h"


/* return an HTTPHeader linked list which must be freed by the caller
 * (see http.h for details of HTTPHeaders). Cannot fail.
 */
#ifdef POST_SUPPORT
List *unpack_post_headers(Octstr *headers);
#endif

List *unpack_headers(Octstr *headers);


/* Outputs unpacked headers into own string
 * the string has format 'Header1: value, value2, value3\r\nHeader2: ....'
 */
Octstr *output_headers(List *uhdrs);



#endif
