/*
 * wsp_headers.h - WSP PDU headers implementation header
 *
 * Kalle Marjola <rpr@wapit.com>
 */

#ifndef WSP_HEADERS_H
#define WSP_HEADERS_H

#include "gwlib/gwlib.h"

#define WSP_FIELD_VALUE_NUL_STRING	1
#define WSP_FIELD_VALUE_ENCODED 	2
#define WSP_FIELD_VALUE_DATA		3
#define WSP_FIELD_VALUE_NONE		4 /* secondary_field_value only */

/* The value defined as Quote in 8.4.2.1 */
#define WSP_QUOTE  127

/* Largest value that will fit in a Short-integer encoding */
#define MAX_SHORT_INTEGER 127

/* Marker values used in the encoding */
#define BASIC_AUTHENTICATION 128
#define ABSOLUTE_TIME 128
#define RELATIVE_TIME 129
#define BYTE_RANGE 128
#define SUFFIX_BYTE_RANGE 129

/* Use this value for Expires headers if we can't parse the expiration
 * date.  It's about one day after the start of the epoch.  We don't
 * use the exact start of the epoch because some clients have trouble
 * with that. */
#define LONG_AGO_VALUE 100000

/* LIST is a comma-separated list such as is described in the "#rule"
 * entry of RFC2616 section 2.1. */
#define LIST 1
/* BROKEN_LIST is a list of "challenge" or "credentials" elements
 * such as described in RFC2617.  I call it broken because the
 * parameters are separated with commas, instead of with semicolons
 * like everywhere else in HTTP.  Parsing is more difficult because
 * commas are also used to separate list elements. */
#define BROKEN_LIST 2

#define TABLE_SIZE(table) ((long)(sizeof(table) / sizeof(table[0])))

struct parameter
{
    Octstr *key;
    Octstr *value;
};
typedef struct parameter Parameter;

typedef int header_pack_func_t(Octstr *packed, Octstr *value);

struct headerinfo
{
    /* The WSP_HEADER_* enumeration value for this header */
    int header;
    header_pack_func_t *func;
    /* True if this header type allows multiple elements per
     * header on the HTTP side. */
    int allows_list;
};

/* All WSP packing/unpacking routines that are exported for use within
 * external modules, ie. MMS encoding/decoding.
 */
int wsp_field_value(ParseContext *context, int *well_known_value);
void wsp_skip_field_value(ParseContext *context);
int wsp_secondary_field_value(ParseContext *context, long *result);
void parm_destroy_item(void *parm);
/* unpacking */
Octstr *wsp_unpack_integer_value(ParseContext *context);
Octstr *wsp_unpack_version_value(long value);
void wsp_unpack_all_parameters(ParseContext *context, Octstr *decoded);
Octstr *wsp_unpack_date_value(ParseContext *context);
void wsp_unpack_well_known_field(List *unpacked, int field_type,
                                 ParseContext *context);
void wsp_unpack_app_header(List *unpacked, ParseContext *context);
/* packing */
int wsp_pack_date(Octstr *packet, Octstr *value);
int wsp_pack_retry_after(Octstr *packet, Octstr *value);
int wsp_pack_text(Octstr *packet, Octstr *value);
int wsp_pack_integer_string(Octstr *packet, Octstr *value);
int wsp_pack_version_value(Octstr *packet, Octstr *value);

/* Return an HTTPHeader linked list which must be freed by the caller
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
