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
 * The second argument is true if the headers will have a leading
 * Content-Type field.  Some WSP PDUs encode Content-Type separately
 * this way for historical reasons.
 */
List *unpack_headers(Octstr *headers, int content_type);

#endif
