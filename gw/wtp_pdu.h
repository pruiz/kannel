/* wtp_pdu.h - definitions for unpacked WTP protocol data units
 *
 * This file generates a structure definition and some function
 * declarations from wtp_pdu.def, using preprocessor magic.
 *
 * Richard Braakman <dark@wapit.com>
 */

struct wtp_tpi {
	int type;
	Octstr *data;
};
typedef struct wtp_tpi WTP_TPI;

/* Enumerate the symbolic names of the PDUs */
enum wtp_pdu_types {
#define PDU(name, docstring, fields, is_valid) name,
#include "wtp_pdu.def"
#undef PDU
};

struct wtp_pdu {
	int type;
	List *options; /* List of WTP_TPI */

	union {
/* For each PDU, declare a structure with its fields, named after the PDU */
#define PDU(name, docstring, fields, is_valid) struct name { fields } name;
#define UINT(field, docstring, bits) unsigned long field;
#define UINTVAR(field, docstring) unsigned long field;
#define OCTSTR(field, docstring, lengthfield) Octstr *field;
#define REST(field, docstring) Octstr *field;
#define TYPE(bits, value)
#define RESERVED(bits)
#define TPI(confield)
#include "wtp_pdu.def"
#undef TPI
#undef RESERVED
#undef TYPE
#undef REST
#undef OCTSTR
#undef UINTVAR
#undef UINT
#undef PDU
	} u;
};
typedef struct wtp_pdu WTP_PDU;

WTP_PDU *wtp_pdu_unpack(Octstr *data);
Octstr *wtp_pdu_pack(WTP_PDU *pdu);
void wtp_pdu_dump(WTP_PDU *pdu, int level);
void wtp_pdu_destroy(WTP_PDU *pdu);
void wtp_tpi_destroy(WTP_TPI *tpi);
