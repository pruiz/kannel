/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2003 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * smpp_pdu.c - parse and generate SMPP PDUs
 *
 * Lars Wirzenius
 */


#include <string.h>
#include "smpp_pdu.h"

#define MIN_SMPP_PDU_LEN    (4*4)
/* old value was (1024). We need more because message_payload can be up to 64K octets*/
#define MAX_SMPP_PDU_LEN    (7424) 


static long decode_integer(Octstr *os, long pos, int octets)
{
    unsigned long u;
    int i;

    if (octstr_len(os) < pos + octets) 
        return -1;

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
	warning(0, "SMPP: PDU NUL terminated string has no NUL.");
    	return NULL;
    }
    if (*pos + max_octets < nul) {
	error(0, "SMPP: PDU NUL terminated string longer than allowed.");
    	return NULL;
    }
    data = (nul - *pos > 0) ? octstr_copy(os, *pos, nul - *pos) : NULL;
    *pos = nul + 1;
    return data;
}


SMPP_PDU *smpp_pdu_create(unsigned long type, unsigned long seq_no)
{
    SMPP_PDU *pdu;

    pdu = gw_malloc(sizeof(*pdu));
    pdu->type = type;

    switch (type) {
    #define OPTIONAL_BEGIN(num_expected) \
    	p->optional_parameters = dict_create(num_expected, (void (*)(void *))octstr_destroy);
    #define TLV(tag_id, min_len, max_len)
    #define OPTIONAL_END
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
    #define OPTIONAL_BEGIN(num_expected) dict_destroy(p->optional_parameters);
    #define TLV(tag_id, min_len, max_len)
    #define OPTIONAL_END
    #define INTEGER(name, octets) p->name = 0; /* Make sure "p" is used */
    #define NULTERMINATED(name, max_octets) octstr_destroy(p->name);
    #define OCTETS(name, field_giving_octets) octstr_destroy(p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type, internal error while destroying.");
    }
    gw_free(pdu);
}


Octstr *smpp_pdu_pack(SMPP_PDU *pdu)
{
    Octstr *os;
    Octstr *temp;

    os = octstr_create("");

    gw_assert(pdu != NULL);

    /*
     * Fix lengths of octet string fields.
     */
    switch (pdu->type) {
    #define OPTIONAL_BEGIN(num_expected)
    #define TLV(tag_id, min_len, max_len)
    #define OPTIONAL_END
    #define INTEGER(name, octets) p = *(&p);
    #define NULTERMINATED(name, max_octets) p = *(&p);
    #define OCTETS(name, field_giving_octets) \
    	p->field_giving_octets = octstr_len(p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type, internal error while packing.");
    }

    switch (pdu->type) {
    #define OPTIONAL_BEGIN(num_expected)
    #define TLV(tag_id, min_len, max_len)                                               \
        {   /* Add optional parameter - if existing */                                  \
            short tag_id_buffer = tag_id;                                               \
            Octstr *opt_tag = octstr_create_from_data((char*) &tag_id_buffer, 2);       \
            Octstr *opt_val = dict_get(p->optional_parameters, opt_tag);                \
            if (opt_val != NULL) {                                                      \
                long opt_len = octstr_len(opt_val);                                     \
                gw_assert(min_len == -1 || (min_len <= opt_len && opt_len <= max_len)); \
                octstr_append(os, opt_tag);                                             \
                octstr_append_data(os, (char*) &opt_len, 2);                            \
                octstr_append(os, opt_val);                                             \
            }                                                                           \
            octstr_destroy(opt_tag);                                                    \
        } 
    #define OPTIONAL_END
    #define INTEGER(name, octets) \
    	append_encoded_integer(os, p->name, octets);
    #define NULTERMINATED(name, max_octets) \
        if (p->name != NULL) { \
            if (octstr_len(p->name) >= max_octets) { \
                warning(0, "SMPP: PDU element <%s> to long " \
                        "(length is %ld, should be %d)", \
                        #name, octstr_len(p->name), max_octets); \
                temp = octstr_copy(p->name, 0, max_octets-1); \
            } else \
                temp = octstr_duplicate(p->name); \
            octstr_append(os, temp); \
            octstr_destroy(temp); \
        } \
        octstr_append_char(os, '\0');
    #define OCTETS(name, field_giving_octets) \
    	octstr_append(os, p->name);
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type, internal error while packing.");
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
    long len, pos;

    len = octstr_len(data_without_len);

    if (len < 4) {
        error(0, "SMPP: PDU was too short (%ld bytes).",
              octstr_len(data_without_len));
        return NULL;
    }

    /* get the PDU type */
    if ((type = decode_integer(data_without_len, 0, 4)) == -1)
        return NULL;

    /* create a coresponding representation structure */
    pdu = smpp_pdu_create(type, 0);
    if (pdu == NULL)
        return NULL;

    pos = 0;

    switch (type) {
    #define OPTIONAL_BEGIN(num_expected)                                                \
        {   /* Read optional parameters */                                              \
            while (pos+4 <= len) {                                                      \
                unsigned long opt_tag, opt_len;                                         \
                Octstr *opt_val = NULL;                                                 \
                Octstr *tag_str = NULL;                                                 \
                opt_tag = decode_integer(data_without_len, pos, 2); pos += 2;           \
                debug("sms.smpp", 0, "Optional parameter tag (0x%04lx)", opt_tag);      \
                opt_len = decode_integer(data_without_len, pos, 2); pos += 2;           \
                debug("sms.smpp", 0, "Optional parameter length read as %ld", opt_len);
    #define TLV(tag_id, min_len, max_len)                                                                          \
                if (tag_id == opt_tag) {                                                                           \
                    if ((min_len != -1 && opt_len < min_len) || (max_len != -1 && opt_len > max_len) ||            \
                        (pos+opt_len > len)) {                                                                     \
                        error(0, "SMPP: Optional field (%s) with invalid length (%ld) dropped.", #tag_id, opt_len);\
                        pos += opt_len;                                                                            \
                        continue;                                                                                  \
                    }                                                                                              \
                    opt_val = octstr_copy(data_without_len, pos, opt_len); pos += opt_len;                         \
                    debug("sms.smpp", 0, "Optional parameter value (%s)", octstr_get_cstr(opt_val));               \
                    tag_str = octstr_create_from_data((char*) &opt_tag, 2);                                        \
                    dict_put(p->optional_parameters, tag_str, opt_val);                                            \
                    octstr_destroy(tag_str);                                                                       \
                    opt_val = NULL;                                                                                \
                } else 
    #define OPTIONAL_END                                                                           \
    		{                                                             \
		    error(0, "SMPP: Unknown optional parameter (0x%04lx) for PDU type (%s) received!", \
		            opt_tag, pdu->type_name);                                              \
		}                                                                                  \
            }                                                                                      \
        } 
    #define INTEGER(name, octets) \
    	p->name = decode_integer(data_without_len, pos, octets); \
	pos += octets;
    #define NULTERMINATED(name, max_octets) \
    	p->name = copy_until_nul(data_without_len, &pos, max_octets);
    #define OCTETS(name, field_giving_octets) \
    	p->name = octstr_copy(data_without_len, pos, \
	    	    	      p->field_giving_octets); \
        if (p->field_giving_octets != (unsigned long) octstr_len(p->name)) { \
            error(0, "smpp_pdu: error while unpacking 'short_message', " \
                     "len is %ld but should have been %ld, dropping.", \
                     octstr_len(p->name), p->field_giving_octets); \
            return NULL; \
        } else { \
            pos += p->field_giving_octets; \
        }
    #define PDU(name, id, fields) \
    	case id: { struct name *p = &pdu->u.name; fields } break;
    #include "smpp_pdu.def"
    default:
    	error(0, "Unknown SMPP_PDU type, internal error while unpacking.");
    }

    return pdu;
}


void smpp_pdu_dump(SMPP_PDU *pdu)
{
    debug("sms.smpp", 0, "SMPP PDU %p dump:", (void *) pdu);
    debug("sms.smpp", 0, "  type_name: %s", pdu->type_name);
    switch (pdu->type) {
    #define OPTIONAL_BEGIN(num_expected) \
	if (p->optional_parameters != NULL) { \
	    Octstr *key = NULL, *tag_val = NULL;
            unsigned long id;
    #define TLV(tag_id, min_len, max_len) \
            id = tag_id; \
            key = octstr_create_from_data((char*)&id, 2); \
            tag_val = dict_get(p->optional_parameters, key); \
            if (tag_val != NULL) { \
                debug("sms.smpp",0,"  %s: ", #tag_id); \
                debug("sms.smpp",0,"    tag: 0x%04lx", tag_id); \
                debug("sms.smpp",0,"    length: 0x%04lx", \
                      octstr_len(tag_val)); \
		        octstr_dump_short(tag_val, 2, "  value"); \
            } \
            octstr_destroy(key);
    #define OPTIONAL_END \
	}
    #define INTEGER(name, octets) \
    	debug("sms.smpp", 0, "  %s: %lu = 0x%08lx", #name, p->name, p->name);
    #define NULTERMINATED(name, max_octets) \
	octstr_dump_short(p->name, 2, #name);
    #define OCTETS(name, field_giving_octets) \
        octstr_dump_short(p->name, 2, #name);
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
	      len, (long) MAX_SMPP_PDU_LEN);
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


/*
 * Return error string for given error code
 * NOTE: If you add new error strings here please use
 *       error strings from SMPP spec. and please keep
 *       error codes in switch statement sorted by error
 *       code ID.
 */
const char *smpp_error_to_string(enum SMPP_ERROR_MESSAGES error)
{
    switch (error) {
        case SMPP_ESME_ROK:
	    return "OK";
        case SMPP_ESME_RINVMSGLEN:
	    return "Message Length is invalid";
        case SMPP_ESME_RINVCMDLEN:
	    return "Command Length is invalid";
        case SMPP_ESME_RINVCMDID:
	    return "Invalid Command ID";
        case SMPP_ESME_RINVBNDSTS:
	    return "Incorrect BIND Status for given command";
        case SMPP_ESME_RALYNBD:
	    return "ESME Already in Bound State";
        case SMPP_ESME_RINVREGDLVFLG:
	    return "Invalid Registered Delivery Flag";
        case SMPP_ESME_RSYSERR:
	    return "System Error";
        case SMPP_ESME_RINVSRCADR:
	    return "Invalid Source Address";
        case SMPP_ESME_RINVDSTADR:
	    return "Invalid Dest Address";
        case SMPP_ESME_RBINDFAIL:
	    return "Bind Failed";
        case SMPP_ESME_RINVPASWD:
	    return "Invalid Password";
        case SMPP_ESME_RINVSYSID:
	    return "Invalid System ID";
        case SMPP_ESME_RMSGQFUL:
	    return "Message Queue Full";
        case SMPP_ESME_RINVESMCLASS:
	    return "Invalid esm_class field data";
        case SMPP_ESME_RINVSRCTON:
	    return "Invalid Source Address TON"; 
        case SMPP_ESME_RTHROTTLED:
	    return "Throttling error";
        case SMPP_ESME_RINVSCHED:
	    return "Invalid Scheduled Delivery Time";
        case SMPP_ESME_RINVEXPIRY:
	    return "Invalid message validity period";
        case SMPP_ESME_RX_T_APPN:
	    return "ESME Receiver Temporary App Error Code";
        case SMPP_ESME_RX_P_APPN:
	    return "ESME Receiver Permanent App Error Code";
        case SMPP_ESME_RX_R_APPN:
	    return "ESME Receiver Reject Message Error Code";
        case SMPP_ESME_ROPTPARNOTALLWD:
	    return "Optional Parameter not allowed";
	case SMPP_ESME_RUNKNOWNERR:
	    return "Unknown Error";
	default:
	    return "Unknown/Reserved";
    }
}
