/*
 * smpp_pdu.h - declarations for SMPP PDUs
 *
 * Lars Wirzenius
 */


#ifndef SMPP_PDU_H
#define SMPP_PDU_H


#include "gwlib/gwlib.h"


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


/******************************************************************************
* Numering Plan Indicator and Type of Number codes from 
* GSM 03.40 Version 5.3.0 Section 9.1.2.5.
* http://www.etsi.org/
*/
#define GSM_ADDR_TON_UNKNOWN          0x00000000
#define GSM_ADDR_TON_INTERNATIONAL    0x00000001
#define GSM_ADDR_TON_NATIONAL         0x00000002
#define GSM_ADDR_TON_NETWORKSPECIFIC  0x00000003
#define GSM_ADDR_TON_SUBSCRIBER       0x00000004
#define GSM_ADDR_TON_ALPHANUMERIC     0x00000005 /* GSM TS 03.38 */
#define GSM_ADDR_TON_ABBREVIATED      0x00000006
#define GSM_ADDR_TON_EXTENSION        0x00000007 /* Reserved */

#define GSM_ADDR_NPI_UNKNOWN          0x00000000
#define GSM_ADDR_NPI_E164             0x00000001 
#define GSM_ADDR_NPI_X121             0x00000003
#define GSM_ADDR_NPI_TELEX            0x00000004
#define GSM_ADDR_NPI_NATIONAL         0x00000008
#define GSM_ADDR_NPI_PRIVATE          0x00000009
#define GSM_ADDR_NPI_ERMES            0x0000000A /* ETSI DE/PS 3 01-3 */
#define GSM_ADDR_NPI_EXTENSION        0x0000000F /* Reserved */

/******************************************************************************
 * esm_class parameters
 */
#define ESM_CLASS_DEFAULT_SMSC_MODE        0x00000000
#define ESM_CLASS_DATAGRAM_MODE            0x00000001
#define ESM_CLASS_FORWARD_MODE             0x00000002
#define ESM_CLASS_STORE_AND_FORWARD_MODE   0x00000003
#define ESM_CLASS_DELIVERY_ACK             0x00000008
#define ESM_CLASS_USER_ACK                 0x00000010
#define ESM_CLASS_UDH_INDICATOR            0x00000040
#define ESM_CLASS_RPI                      0x00000080
#define ESM_CLASS_UDH_AND_RPI              0x000000C0


SMPP_PDU *smpp_pdu_create(unsigned long type, unsigned long seq_no);
void smpp_pdu_destroy(SMPP_PDU *pdu);
int smpp_pdu_is_valid(SMPP_PDU *pdu); /* XXX */
Octstr *smpp_pdu_pack(SMPP_PDU *pdu);
SMPP_PDU *smpp_pdu_unpack(Octstr *data_without_len);
void smpp_pdu_dump(SMPP_PDU *pdu);

long smpp_pdu_read_len(Connection *conn);
Octstr *smpp_pdu_read_data(Connection *conn, long len);


#endif
