/*
 * smpp_pdu.h - declarations for SMPP PDUs
 *
 * Lars Wirzenius
 */


#ifndef SMPP_PDU_H
#define SMPP_PDU_H


#include "gwlib/gwlib.h"


enum {
    SMPP_ESM_CLASS_UDH_INDICATOR = 0x43,
};


enum {
    #define INTEGER(name, octets)
    #define NULTERMINATED(name, max_octets)
    #define OCTETS(name, field_giving_octets)
    #define PDU(name, id, fields) name = id,
    #include "smpp_pdu.def"
    SMPP_PDU_DUMMY_TYPE
};


typedef struct SMPP_PDU SMPP_PDU;
struct SMPP_PDU {
    unsigned long type;
    const char *type_name;
    union {
	#define INTEGER(name, octets) unsigned long name;
	#define NULTERMINATED(name, max_octets) Octstr *name;
	#define OCTETS(name, field_giving_octets) Octstr *name;
	#define PDU(name, id, fields) struct name { fields } name;
	#include "smpp_pdu.def"
    } u;
};


SMPP_PDU *smpp_pdu_create(unsigned long type, unsigned long seq_no);
void smpp_pdu_destroy(SMPP_PDU *pdu);
int smpp_pdu_is_valid(SMPP_PDU *pdu); /* XXX */
Octstr *smpp_pdu_pack(SMPP_PDU *pdu);
SMPP_PDU *smpp_pdu_unpack(Octstr *data_without_len);
void smpp_pdu_dump(SMPP_PDU *pdu);

long smpp_pdu_read_len(Connection *conn);
Octstr *smpp_pdu_read_data(Connection *conn, long len);


#endif
