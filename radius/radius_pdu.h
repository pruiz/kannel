/*
 * radius_pdu.h - declarations for RADIUS Accounting PDUs
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */


#ifndef RADIUS_PDU_H
#define RADIUS_PDU_H


#include "gwlib/gwlib.h"
#include "gwlib/dict.h"

/* attribute types */
enum {
    t_int, t_string, t_ipaddr
};

enum {
    #define ATTR(attr, type, string, min, max)
    #define UNASSIGNED(attr)
    #define ATTRIBUTES(fields)
    #include "radius_attributes.def"
    #define INTEGER(name, octets)
    #define OCTETS(name, field_giving_octets)
    #define PDU(name, id, fields) name = id,
    #include "radius_pdu.def"
    RADIUS_PDU_DUMMY_TYPE
};


typedef struct RADIUS_PDU RADIUS_PDU;
struct RADIUS_PDU {
    int type;
    const char *type_name;
    Dict *attr;
    union {
    #define ATTR(attr, type, string, min, max)
    #define UNASSIGNED(attr)
	#define ATTRIBUTES(fields)
    #include "radius_attributes.def"
	#define INTEGER(name, octets) unsigned long name;
	#define NULTERMINATED(name, max_octets) Octstr *name;
	#define OCTETS(name, field_giving_octets) Octstr *name;
	#define PDU(name, id, fields) struct name { fields } name;
	#include "radius_pdu.def"
    } u;
};


RADIUS_PDU *radius_pdu_create(int type, RADIUS_PDU *req);
void radius_pdu_destroy(RADIUS_PDU *pdu);

int radius_authenticate_pdu(RADIUS_PDU *pdu, Octstr **data, Octstr *secret);

Octstr *radius_pdu_pack(RADIUS_PDU *pdu);
RADIUS_PDU *radius_pdu_unpack(Octstr *data_without_len);

void radius_pdu_dump(RADIUS_PDU *pdu);

/*
 * Returns the value of an RADIUS attribute inside a PDU as Octstr.
 * If the attribute was not present in the PDU, it returns NULL.
 */
Octstr *radius_get_attribute(RADIUS_PDU *pdu, Octstr *attribute);

#endif
