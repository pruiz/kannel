/* wsp_caps.h - interface to WSP capability negotiation
 *
 * Richard Braakman
 */

#ifndef WSP_CAPS_H
#define WSP_CAPS_H

#include "gwlib/gwlib.h"

struct capability {
	/* One or the other of these is set.  id is only meaningful
	 * if name is NULL.  (Unfortunately the WSP spec does not
	 * really assign names to the numeric ids, so we can't translate
	 * them all to text.) */
	int id;
	Octstr *name;

	/* Raw data for this capability.  Can be NULL if there is none. */
	Octstr *data;

	/* If data is NULL, this field determines if the request should
	 * be accepted or rejected. */
	int accept;
};

typedef struct capability Capability;

/* See table 37 */
enum known_caps {
	WSP_CAPS_CLIENT_SDU_SIZE = 0,
	WSP_CAPS_SERVER_SDU_SIZE = 1,
	WSP_CAPS_PROTOCOL_OPTIONS = 2,
	WSP_CAPS_METHOD_MOR = 3,
	WSP_CAPS_PUSH_MOR = 4,
	WSP_CAPS_EXTENDED_METHODS = 5,
	WSP_CAPS_HEADER_CODE_PAGES = 6,
	WSP_CAPS_ALIASES = 7,
	WSP_NUM_CAPS
};

/* Create a new Capability structure.  For numbered capabilities (which
 * is all of the known ones), use NULL for the name.  The data may also
 * be NULL. */
Capability *wsp_cap_create(int id, Octstr *name, Octstr *data);
void wsp_cap_destroy(Capability *cap);

void wsp_cap_dump(Capability *cap);
void wsp_cap_dump_list(List *caps_list);

/* Destroy all Capabilities in a list, as well as the list itself. */
void wsp_cap_destroy_list(List *caps_list);

/* Duplicate a list of Capabilities */
List *wsp_cap_duplicate_list(List *cap);
Capability *wsp_cap_duplicate(Capability *cap);

/* Return a list of Capability structures */
List *wsp_cap_unpack_list(Octstr *caps);

/* Encode a list of Capability structures according to the WSP spec */
Octstr *wsp_cap_pack_list(List *caps_list);

/* Access functions.  All of them return the number of requests that 
 * match the capability being searched for, and if they have an output
 * parameter, they set it to the value of the first such request. */
int wsp_cap_count(List *caps_list, int id, Octstr *name);
int wsp_cap_get_client_sdu(List *caps_list, unsigned long *sdu);
int wsp_cap_get_server_sdu(List *caps_list, unsigned long *sdu);
int wsp_cap_get_method_mor(List *caps_list, unsigned long *mor);
int wsp_cap_get_push_mor(List *caps_list, unsigned long *mor);

#endif
