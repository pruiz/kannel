/*
 * smasi_pdu.h - declarations for SMASI PDUs
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */


#ifndef SMASI_PDU_H
#define SMASI_PDU_H

#include "gwlib/gwlib.h"

/* 
 * Any PDUs that are below this ID will be packed with a 
 * prefixed hyphen.
 */
#define SMASI_HYPHEN_ID     0x00000010

enum {
    #define NONTERMINATED(name)
    #define COMATERMINATED(name)
    #define PDU(name, id, fields) name = id,
    #include "smasi_pdu.def"
    SMASI_PDU_DUMMY_TYPE
};

typedef struct SMASI_PDU SMASI_PDU;
struct SMASI_PDU {
    unsigned long type;
    const char *type_name;
    unsigned int needs_hyphen;
    union {
    #define NONTERMINATED(name) Octstr *name;
    #define COMATERMINATED(name) Octstr *name;
    #define PDU(name, id, fields) struct name { fields } name;
    #include "smasi_pdu.def"
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


SMASI_PDU *smasi_pdu_create(unsigned long type);
void smasi_pdu_destroy(SMASI_PDU *pdu);
int smasi_pdu_is_valid(SMASI_PDU *pdu); /* XXX */
Octstr *smasi_pdu_pack(SMASI_PDU *pdu);
SMASI_PDU *smasi_pdu_unpack(Octstr *data_without_len);
void smasi_pdu_dump(SMASI_PDU *pdu);

Octstr *smasi_pdu_read(Connection *conn);


#endif
