/*
 * smpp_pdu.c - parse and generate SMPP PDUs
 *
 * Lars Wirzenius
 */


#include <string.h>
#include "smpp_pdu.h"


#define MIN_SMPP_PDU_LEN    (4*4)
#define MAX_SMPP_PDU_LEN    (1024)


static unsigned long decode_integer(Octstr *os, long pos, int octets)
{
    unsigned long u;
    int i;
    
    gw_assert(octstr_len(os) >= octets);

    u = 0;
    for (i = 0; i < octets; ++i)
    	u = (u << 8) | octstr_get_char(os, pos + i);

    return u;
}


static void append_encoded_integer(Octstr *os, unsigned long u, long octets)
{
    long i;

    for (i = 0; i < octets; ++i)
    	octstr_append_char(os, (u >> ((octets - i - 1) * 8)) & 0xFF);
}


static Octstr *copy_until_nul(Octstr *os, long *pos, long max_octets)
{
    long nul;
    Octstr *data;

    nul = octstr_search_char(os, '\0', *pos);
    if (nul == -1) {
	error(0, "SMPP: PDU NUL terminated string has no NUL.");
    	return NULL;
    }
    if (*pos + max_octets < nul) {
	error(0, "SMPP: PDU NUL terminated string longer than allowed.");
    	return NULL;
    }
    data = octstr_copy(os, *pos, nul - *pos);
    *pos = nul + 1;
    return data;
}


SMPP_PDU *smpp_pdu_create(unsigned long type, unsigned long seq_no)
{
    SMPP_PDU *pdu;
    
    pdu = gw_malloc(sizeof(*pdu));
    pdu->type = type;
    
    switch (type) {
    #define INTEGER(name, octets) \
   	if (strcmp(#name, "command_id") == 0) p->name = type; \
    	else if (strcmp(#name, "sequence_number") == 0) p->name = seq_no; \
    	else p->name = 0;
    #define NULTERMINATED(name, max_octets) p->name = NULL;
    #define OCTETS(name, field_giving_octetst) p->name = NULL;
    #define PDU(name, id, fields) \
    	case id: { \
	    struct name *p = &pdu->u.name; \
	    pdu->type_name = #name; \
	    fields \
	} break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type, internal error.");
    	gw_free(pdu);
	return NULL;
    }
    
    return pdu;
}


void smpp_pdu_destroy(SMPP_PDU *pdu)
{
    if (pdu == NULL)
    	return;

    switch (pdu->type) {
    #define INTEGER(name, octets) p->name = 0; /* Make sure "p" is used */
    #define NULTERMINATED(name, max_octets) octstr_destroy(p->name);
    #define OCTETS(name, field_giving_octets) octstr_destroy(p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	panic(0, "Unknown SMPP_PDU type, internal error while destroying.");
    }
    gw_free(pdu);
}


Octstr *smpp_pdu_pack(SMPP_PDU *pdu)
{
    Octstr *os;
    Octstr *temp;
    
    os = octstr_create("");

    /*
     * Fix lengths of octet string fields.
     */
    switch (pdu->type) {
    #define INTEGER(name, octets) p = *(&p);
    #define NULTERMINATED(name, max_octets) p = *(&p);
    #define OCTETS(name, field_giving_octets) \
    	p->field_giving_octets = octstr_len(p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	panic(0, "Unknown SMPP_PDU type, internal error while packing.");
    }

    switch (pdu->type) {
    #define INTEGER(name, octets) \
    	append_encoded_integer(os, p->name, octets);
    #define NULTERMINATED(name, max_octets) \
    	gw_assert(octstr_len(p->name) < max_octets); \
	if (p->name != NULL) octstr_append(os, p->name); \
	octstr_append_char(os, '\0');
    #define OCTETS(name, field_giving_octets) \
    	octstr_append(os, p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	panic(0, "Unknown SMPP_PDU type, internal error while packing.");
    }

    temp = octstr_create("");
    append_encoded_integer(temp, octstr_len(os) + 4, 4);
    octstr_insert(os, temp, 0);
    octstr_destroy(temp);

    return os;
}


SMPP_PDU *smpp_pdu_unpack(Octstr *data_without_len)
{
    SMPP_PDU *pdu;
    unsigned long type;
    long pos;
    
    if (octstr_len(data_without_len) < 4) {
	error(0, "SMPP: PDU was too short (%ld bytes).", 
	      octstr_len(data_without_len));
	return NULL;
    }

    type = decode_integer(data_without_len, 0, 4);
    pdu = smpp_pdu_create(type, 0);
    if (pdu == NULL)
    	return NULL;
    
    pos = 0;

    switch (type) {
    #define INTEGER(name, octets) \
    	p->name = decode_integer(data_without_len, pos, octets); \
	pos += octets;
    #define NULTERMINATED(name, max_octets) \
    	p->name = copy_until_nul(data_without_len, &pos, max_octets); \
	if (p->name == NULL) { smpp_pdu_destroy(pdu); return NULL; }
    #define OCTETS(name, field_giving_octets) \
    	p->name = octstr_copy(data_without_len, pos, \
	    	    	      p->field_giving_octets); \
    	gw_assert(p->field_giving_octets == \
	    	  (unsigned long) octstr_len(p->name)); \
	pos += p->field_giving_octets;
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	panic(0, "Unknown SMPP_PDU type, internal error while unpacking.");
    }

    return pdu;
}


void smpp_pdu_dump(SMPP_PDU *pdu)
{
    debug("sms.smpp", 0, "SMPP PDU %p dump:", (void *) pdu);
    debug("sms.smpp", 0, "  type_name: %s", pdu->type_name);
    switch (pdu->type) {
    #define INTEGER(name, octets) \
    	debug("sms.smpp", 0, "  %s: %lu = 0x%08lx", #name, p->name, p->name);
    #define NULTERMINATED(name, max_octets) \
    	debug("sms.smpp", 0, "  %s:", #name); \
	if (p->name != NULL) \
	    octstr_dump(p->name, 4);
    #define OCTETS(name, field_giving_octets) \
    	debug("sms.smpp", 0, "  %s:", #name); \
	if (p->name != NULL) \
	    octstr_dump(p->name, 4);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type, internal error.");
	break;
    }
    debug("sms.smpp", 0, "SMPP PDU dump ends.");
}


long smpp_pdu_read_len(Connection *conn)
{
    Octstr *os;
    char buf[4];    /* The length is 4 octets. */
    long len;
    
    os = conn_read_fixed(conn, sizeof(buf));
    if (os == NULL)
    	return 0;
    octstr_get_many_chars(buf, os, 0, sizeof(buf));
    octstr_destroy(os);
    len = decode_network_long(buf);
    if (len < MIN_SMPP_PDU_LEN) {
	error(0, "SMPP: PDU length was too small (%ld, minimum is %ld).",
	      len, (long) MIN_SMPP_PDU_LEN);
    	return -1;
    }
    if (len > MAX_SMPP_PDU_LEN) {
	error(0, "SMPP: PDU length was too large (%ld, maximum is %ld).",
	      len, (long) MIN_SMPP_PDU_LEN);
    	return -1;
    }
    return len;
}


Octstr *smpp_pdu_read_data(Connection *conn, long len)
{
    Octstr *os;
    
    os = conn_read_fixed(conn, len - 4);    /* `len' includes itself. */
    return os;
}
