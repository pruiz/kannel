/*
 * wsp_headers.h - WSP PDU headers implementation header
 *
 * Kalle Marjola <rpr@wapit.com>
 */

#ifndef WSP_HEADERS_H
#define WSP_HEADERS_H

#include "gwlib/gwlib.h"

/* return an HTTPHeader linked list which must be freed by the caller
 * (see http.h for details of HTTPHeaders). Cannot fail.
 * The second argument is true if the headers will have a leading
 * Content-Type field.  Some WSP PDUs encode Content-Type separately
 * this way for historical reasons.
 */
List *wsp_headers_unpack(Octstr *headers, int content_type);

/* Take a List of headers, encode them according to the WSP spec,
 * and return the encoded headers as an Octstr. 
 * The second argument is true if the encoded headers should have
 * a leading content-type field.  See the note for wsp_headers_unpack. */
Octstr *wsp_headers_pack(List *headers, int separate_content_type);

#endif
