/* 
 * smsc_smpp.c - SMPP v3.3 and v3.4 implementation 
 * 
 * Lars Wirzenius
 * Stipe Tolj <tolj@wapme-systems.de>
 */ 
  
/* XXX check SMSCConn conformance */ 
/* XXX UDH reception */ 
/* XXX check UDH sending fields esm_class and data_coding from GSM specs */ 
/* XXX charset conversions on incoming messages (didn't work earlier,  
       either) */ 
/* XXX numbering plans and type of number: check spec */ 
  
#include "gwlib/gwlib.h" 
#include "msg.h" 
#include "smsc_p.h" 
#include "smpp_pdu.h" 
#include "smscconn_p.h" 
#include "bb_smscconn_cb.h" 
#include "sms.h" 
#include "dlr.h" 
 
/* 
 * Select these based on whether you want to dump SMPP PDUs as they are  
 * sent and received or not. Not dumping should be the default in at least 
 * stable releases. 
 */ 

#define DEBUG 1

#ifndef DEBUG 
/* This version doesn't dump. */ 
static void dump_pdu(const char *msg, Octstr *id, SMPP_PDU *pdu) 
{ 
} 
#else 
/* This version does dump. */ 
static void dump_pdu(const char *msg, Octstr *id, SMPP_PDU *pdu) 
{ 
    debug("bb.sms.smpp", 0, "SMPP[%s]: %s", 
          octstr_get_cstr(id), msg); 
    smpp_pdu_dump(pdu); 
} 
#endif 
 
 
/*  
 * Some defaults.
 */ 
 
#define SMPP_ENQUIRE_LINK_INTERVAL  30.0 
#define SMPP_MAX_PENDING_SUBMITS    10 
#define SMPP_DEFAULT_VERSION        0x34
#define SMPP_DEFAULT_PRIORITY       0
#define SMPP_THROTTLING_SLEEP_TIME  15


/*********************************************************************** 
 * Implementation of the actual SMPP protocol: reading and writing 
 * PDUs in the correct order. 
 */ 
 
 
typedef struct { 
    long transmitter; 
    long receiver; 
    List *msgs_to_send; 
    Dict *sent_msgs; 
    List *received_msgs; 
    Counter *message_id_counter; 
    Octstr *host; 
    Octstr *system_type; 
    Octstr *username; 
    Octstr *password; 
    Octstr *address_range; 
    Octstr *my_number; 
    Octstr *service_type;
    int source_addr_ton; 
    int source_addr_npi; 
    int dest_addr_ton; 
    int dest_addr_npi;
    int transmit_port; 
    int receive_port; 
    int quitting; 
    long enquire_link_interval;
    long max_pending_submits;
    int version;
    int priority;       /* set default priority for messages */    
    time_t throttling_err_time;
    int smpp_msg_id_type;  /* msg id in C string, hex or decimal */
    int autodetect_addr;
    Octstr *alt_charset;
    SMSCConn *conn; 
} SMPP; 
 
 
static SMPP *smpp_create(SMSCConn *conn, Octstr *host, int transmit_port,  
    	    	    	 int receive_port, Octstr *system_type,  
                         Octstr *username, Octstr *password, 
    	    	    	 Octstr *address_range,
                         int source_addr_ton, int source_addr_npi,  
                         int dest_addr_ton, int dest_addr_npi, 
                         int enquire_link_interval, 
                         int max_pending_submits, int version, int priority,
                         Octstr *my_number, int smpp_msg_id_type, 
                         int autodetect_addr, Octstr *alt_charset, 
                         Octstr *service_type) 
{ 
    SMPP *smpp; 
     
    smpp = gw_malloc(sizeof(*smpp)); 
    smpp->transmitter = -1; 
    smpp->receiver = -1; 
    smpp->msgs_to_send = list_create(); 
    smpp->sent_msgs = dict_create(16, NULL); 
    list_add_producer(smpp->msgs_to_send); 
    smpp->received_msgs = list_create(); 
    smpp->message_id_counter = counter_create(); 
    counter_increase(smpp->message_id_counter);
    smpp->host = octstr_duplicate(host); 
    smpp->system_type = octstr_duplicate(system_type); 
    smpp->username = octstr_duplicate(username); 
    smpp->password = octstr_duplicate(password); 
    smpp->address_range = octstr_duplicate(address_range); 
    smpp->source_addr_ton = source_addr_ton; 
    smpp->source_addr_npi = source_addr_npi; 
    smpp->dest_addr_ton = dest_addr_ton; 
    smpp->dest_addr_npi = dest_addr_npi; 
    smpp->my_number = octstr_duplicate(my_number); 
    smpp->service_type = octstr_duplicate(service_type);
    smpp->transmit_port = transmit_port; 
    smpp->receive_port = receive_port; 
    smpp->enquire_link_interval = enquire_link_interval;
    smpp->max_pending_submits = max_pending_submits; 
    smpp->quitting = 0; 
    smpp->version = version;
    smpp->priority = priority;
    smpp->conn = conn; 
    smpp->throttling_err_time = 0; 
    smpp->smpp_msg_id_type = smpp_msg_id_type;    
    smpp->autodetect_addr = autodetect_addr;
    smpp->alt_charset = octstr_duplicate(alt_charset);
 
    return smpp; 
} 
 
 
static void smpp_destroy(SMPP *smpp) 
{ 
    if (smpp != NULL) { 
        list_destroy(smpp->msgs_to_send, msg_destroy_item); 
        dict_destroy(smpp->sent_msgs); 
        list_destroy(smpp->received_msgs, msg_destroy_item); 
        counter_destroy(smpp->message_id_counter); 
        octstr_destroy(smpp->host); 
        octstr_destroy(smpp->username); 
        octstr_destroy(smpp->password); 
        octstr_destroy(smpp->system_type); 
        octstr_destroy(smpp->service_type); 
        octstr_destroy(smpp->address_range); 
        octstr_destroy(smpp->my_number);
        octstr_destroy(smpp->alt_charset);
        gw_free(smpp); 
    } 
} 
 
 
/* 
 * Try to read an SMPP PDU from a Connection. Return -1 for error (caller 
 * should close the connection), 0 for no PDU to ready yet, or 1 for PDU 
 * read and unpacked. Return a pointer to the PDU in `*pdu'. Use `*len' 
 * to store the length of the PDU to read (it may be possible to read the 
 * length, but not the rest of the PDU - we need to remember the lenght 
 * for the next call). `*len' should be zero at the first call. 
 */ 
static int read_pdu(SMPP *smpp, Connection *conn, long *len, SMPP_PDU **pdu) 
{ 
    Octstr *os; 
 
    if (*len == 0) { 
        *len = smpp_pdu_read_len(conn); 
        if (*len == -1) { 
            error(0, "SMPP[%s]: Server sent garbage, ignored.",
                  octstr_get_cstr(smpp->conn->id)); 
            return -1; 
        } else if (*len == 0) { 
            if (conn_eof(conn) || conn_read_error(conn)) 
                return -1; 
            return 0; 
        } 
    } 
     
    os = smpp_pdu_read_data(conn, *len); 
    if (os == NULL) { 
        if (conn_eof(conn) || conn_read_error(conn)) 
            return -1; 
        return 0; 
    } 
    *len = 0; 
     
    *pdu = smpp_pdu_unpack(os); 
    if (*pdu == NULL) { 
        error(0, "SMPP[%s]: PDU unpacking failed.",
              octstr_get_cstr(smpp->conn->id)); 
        debug("bb.sms.smpp", 0, "SMPP[%s]: Failed PDU follows.",
              octstr_get_cstr(smpp->conn->id)); 
        octstr_dump(os, 0); 
        octstr_destroy(os); 
        return -1; 
    } 
     
    octstr_destroy(os); 
    return 1; 
} 
 
 
static Msg *pdu_to_msg(SMPP *smpp, SMPP_PDU *pdu) 
{ 
    Msg *msg;
    int udh_offset = 0;
 
    gw_assert(pdu->type == deliver_sm); 
     
    msg = msg_create(sms); 
    msg->sms.sender = pdu->u.deliver_sm.source_addr; 
    pdu->u.deliver_sm.source_addr = NULL; 
    msg->sms.receiver = pdu->u.deliver_sm.destination_addr; 
    pdu->u.deliver_sm.destination_addr = NULL; 

    dcs_to_fields(&msg, pdu->u.deliver_sm.data_coding);

    /* extract UDH sequence if any */
    if (pdu->u.deliver_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR) {
        udh_offset = octstr_get_char(pdu->u.deliver_sm.short_message, 0) + 1; 
        /* check if the UDH offset is of acceptable length, or discard */
        if (udh_offset <= octstr_len(pdu->u.deliver_sm.short_message)) {
            msg->sms.udhdata = 
                octstr_copy(pdu->u.deliver_sm.short_message, 0, udh_offset);
            msg->sms.msgdata = 
                octstr_copy(pdu->u.deliver_sm.short_message, udh_offset,
                            octstr_len(pdu->u.deliver_sm.short_message) - udh_offset);
            octstr_destroy(pdu->u.deliver_sm.short_message);
            msg->sms.coding = DC_8BIT;
        } else {
            /* discard message if UDH length indicator is obvious corrupt */
            error(0, "SMPP[%s]: Mallformed UDH length indicator 0x%03x while message length "
                     "0x%03x. Discarding binary MO message.", octstr_get_cstr(smpp->conn->id), 
                     udh_offset, (unsigned int)octstr_len(pdu->u.deliver_sm.short_message));
            msg_destroy(msg);
            return NULL;
        }
    } else {
        msg->sms.msgdata = pdu->u.deliver_sm.short_message;
    }
    pdu->u.deliver_sm.short_message = NULL;

    /* handle default data coding */
    switch (pdu->u.deliver_sm.data_coding) { 
        case 0x00: /* default SMSC alphabet */
            /* 
             * try to convert from something interesting if specified so
             * unless it was specified binary, ie. UDH indicator was detected
             */
            if (smpp->alt_charset && msg->sms.coding != DC_8BIT) { 
                if (charset_convert(msg->sms.msgdata, octstr_get_cstr(smpp->alt_charset), "ISO-8859-1") != 0)
                    error(0, "Failed to convert msgdata from charset <%s> to <%s>, will leave as is.",
                             octstr_get_cstr(smpp->alt_charset), "ISO-8859-1");
                msg->sms.coding = DC_7BIT;

            } else { /* assume GSM 03.38 7-bit alphabet */
                charset_gsm_to_latin1(msg->sms.msgdata); 
                msg->sms.coding = DC_7BIT;
            }
            break;
        case 0x01: /* ASCII or IA5 - not sure if I need to do anything */
        case 0x02: /* 8 bit binary - do nothing */
        case 0x04: /* 8 bit binary - do nothing */
                break;
        case 0x03: /* ISO-8859-1 - do nothing */
                msg->sms.coding = DC_8BIT; break;
        case 0x05: /* JIS - what do I do with that ? */
                break;
        case 0x06: /* Cyrllic - iso-8859-5, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-5", "UCS-2BE") != 0)
                error(0, "Failed to convert msgdata from cyrllic to UCS-2, will leave as is");
            msg->sms.coding = DC_UCS2; break;
        case 0x07: /* Hebrew iso-8859-8, I'll convert to unicode */
            if (charset_convert(msg->sms.msgdata, "ISO-8859-8", "UCS-2BE") != 0)
                error(0, "Failed to convert msgdata from hebrew to UCS-2, will leave as is");
            msg->sms.coding = DC_UCS2; break;
        case 0x08: /* unicode UCS-2, yey */
            msg->sms.coding = DC_UCS2; break;

            /* 
             * don't much care about the others,
             * you implement them if you feel like it 
             */
        default: 
            /* if we have an UDH indicator, we assume DC_8BIT */
            msg->sms.coding = pdu->u.deliver_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR ?
                DC_8BIT : DC_7BIT;
    }
    msg->sms.pid = pdu->u.deliver_sm.protocol_id; 

    return msg; 
} 
 
 
static long smpp_status_to_smscconn_failure_reason(long status) 
{ 
    switch(status) {
        case SMPP_ESME_RMSGQFUL:
            return SMSCCONN_FAILED_TEMPORARILY;
            break;

        case SMPP_ESME_RTHROTTLED:
            return SMSCCONN_FAILED_TEMPORARILY; 
            break;

        default:
            return SMSCCONN_FAILED_REJECTED; 
    }
} 
 
 
static SMPP_PDU *msg_to_pdu(SMPP *smpp, Msg *msg) 
{ 
    SMPP_PDU *pdu; 
    Octstr *buffer;
    Octstr *relation_UTC_time = NULL;
    struct tm gmtime, localtime, tm;
    int gwqdiff;

    pdu = smpp_pdu_create(submit_sm,  
    	    	    	  counter_increase(smpp->message_id_counter)); 
    	    	    
    pdu->u.submit_sm.source_addr = octstr_duplicate(msg->sms.sender); 
    pdu->u.submit_sm.destination_addr = octstr_duplicate(msg->sms.receiver); 

    /* Set the service type of the outgoing message */
    pdu->u.submit_sm.service_type = octstr_duplicate(smpp->service_type);
  
    /* Check for manual override of source ton and npi values */ 
    if(smpp->source_addr_ton > -1 && smpp->source_addr_npi > -1) { 
        pdu->u.submit_sm.source_addr_ton = smpp->source_addr_ton; 
        pdu->u.submit_sm.source_addr_npi = smpp->source_addr_npi; 
        debug("bb.sms.smpp", 0, "SMPP[%s]: Manually forced source addr ton = %d, source add npi = %d", 
              octstr_get_cstr(smpp->conn->id), smpp->source_addr_ton, 
              smpp->source_addr_npi); 
    } else { 
        /* setup default values */ 
        pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */ 
        pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */ 
    }

    if (smpp->autodetect_addr) {
        /* lets see if its international or alphanumeric sender */ 
        if (octstr_get_char(pdu->u.submit_sm.source_addr, 0) == '+') { 
            if (!octstr_check_range(pdu->u.submit_sm.source_addr, 1, 256, gw_isdigit)) { 
                pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC; /* alphanum */ 
                pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;    /* short code */ 
            } else { 
               /* numeric sender address with + in front -> international (remove the +) */ 
               octstr_delete(pdu->u.submit_sm.source_addr, 0, 1); 
               pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_INTERNATIONAL; 
    	    } 
        } else { 
            if (!octstr_check_range(pdu->u.submit_sm.source_addr,0, 256, gw_isdigit)) { 
                pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC; 
                pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN; 
            } 
        } 
    } 
 
    /* Check for manual override of destination ton and npi values */ 
    if (smpp->dest_addr_ton > -1 && smpp->dest_addr_npi > -1) { 
        pdu->u.submit_sm.dest_addr_ton = smpp->dest_addr_ton; 
        pdu->u.submit_sm.dest_addr_npi = smpp->dest_addr_npi; 
        debug("bb.sms.smpp", 0, "SMPP[%s]: Manually forced dest addr ton = %d, dest add npi = %d", 
              octstr_get_cstr(smpp->conn->id), smpp->dest_addr_ton, 
              smpp->dest_addr_npi); 
    } else { 
        pdu->u.submit_sm.dest_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */ 
        pdu->u.submit_sm.dest_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */ 
    } 
 
    /* 
     * if its a international number starting with +, lets remove the 
     * '+' and set number type to international instead  
     */ 
    if (octstr_get_char(pdu->u.submit_sm.destination_addr,0) == '+') { 
        octstr_delete(pdu->u.submit_sm.destination_addr, 0,1); 
        pdu->u.submit_sm.dest_addr_ton = GSM_ADDR_TON_INTERNATIONAL; 
    } 
 
    /* 
     * set the data coding scheme (DCS) field 
     * check if we have a forced value for this from the smsc-group.
     */
    pdu->u.submit_sm.data_coding = fields_to_dcs(msg, (msg->sms.alt_dcs ? 
        2 - msg->sms.alt_dcs : smpp->conn->alt_dcs));

    /* set protocoll id */
    if(msg->sms.pid) 
        pdu->u.submit_sm.protocol_id = msg->sms.pid; 

    /*
     * set the esm_class field
     * default is store and forward, plus udh and rpi if requested
     */
    pdu->u.submit_sm.esm_class = ESM_CLASS_SUBMIT_STORE_AND_FORWARD_MODE;
    if (octstr_len(msg->sms.udhdata)) 
        pdu->u.submit_sm.esm_class = pdu->u.submit_sm.esm_class |
            ESM_CLASS_SUBMIT_UDH_INDICATOR;
    if (msg->sms.rpi) 
        pdu->u.submit_sm.esm_class = pdu->u.submit_sm.esm_class |
            ESM_CLASS_SUBMIT_RPI;

    /*
     * set data segments and length
     */

    pdu->u.submit_sm.short_message = octstr_duplicate(msg->sms.msgdata); 

    /* 
     * only re-encoding if using default smsc charset that is defined via 
     * alt-charset in smsc group and if MT is not binary
     */
    if (pdu->u.submit_sm.data_coding == 0) {
        /* 
         * convert to the given alternative charset
         * otherwise assume to convert to GSM 03.38 7-bit alphabet
         */
        if (smpp->alt_charset) {
            if (charset_convert(pdu->u.submit_sm.short_message, "ISO-8859-1",
                                octstr_get_cstr(smpp->alt_charset)) != 0)
                error(0, "Failed to convert msgdata from charset <%s> to <%s>, will send as is.", 
                             "ISO-8859-1", octstr_get_cstr(smpp->alt_charset));
        } else {
            charset_latin1_to_gsm(pdu->u.submit_sm.short_message);		 
        }
    }
 
    /* prepend udh if present */
    if (octstr_len(msg->sms.udhdata)) { 
        octstr_insert(pdu->u.submit_sm.short_message, msg->sms.udhdata, 0);
    }

    pdu->u.submit_sm.sm_length = octstr_len(pdu->u.submit_sm.short_message);

    /*
     * check for validity and defered settings
     */
    if (msg->sms.validity || msg->sms.deferred) {

        /* work out 1/4 hour difference between local time and UTC/GMT */
        gmtime = gw_gmtime(time(NULL));
        localtime = gw_localtime(time(NULL));
        gwqdiff = ((localtime.tm_hour - gmtime.tm_hour) * 4)
                  + ((localtime.tm_min - gmtime.tm_min) / 15);
        
        if (gwqdiff >= 0) {
            relation_UTC_time = octstr_create("+");
        } else {
            relation_UTC_time = octstr_create("-");
            gwqdiff *= -1;  /* make absolute */
        }

        if (msg->sms.validity) {
            tm = gw_localtime(time(NULL) + msg->sms.validity * 60);
            buffer = octstr_format("%02d%02d%02d%02d%02d%02d0%02d%1s",
                    tm.tm_year % 100, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    gwqdiff, octstr_get_cstr(relation_UTC_time));
            pdu->u.submit_sm.validity_period = octstr_copy(buffer,0,16);
            octstr_destroy(buffer);
        }

        if (msg->sms.deferred) {
            tm = gw_localtime(time(NULL) + msg->sms.deferred * 60);
            buffer = octstr_format("%02d%02d%02d%02d%02d%02d0%02d%1s",
                    tm.tm_year % 100, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    gwqdiff, octstr_get_cstr(relation_UTC_time));
            pdu->u.submit_sm.schedule_delivery_time = octstr_copy(buffer,0,16);
            octstr_destroy(buffer);
        }
    }

    /* ask for the delivery reports if needed */ 
    if (msg->sms.dlr_mask & (DLR_SUCCESS|DLR_FAIL)) 
        pdu->u.submit_sm.registered_delivery = 1;  

    octstr_destroy(relation_UTC_time);

    /* set priority */
    if (smpp->priority >= 0 && smpp->priority <= 5) {
        pdu->u.submit_sm.priority_flag = smpp->priority;       
    } else {      
        /* default priority is 0 */         
        pdu->u.submit_sm.priority_flag = 0;         
    }              

    return pdu; 
} 
 
 
static void send_enquire_link(SMPP *smpp, Connection *conn, long *last_sent) 
{ 
    SMPP_PDU *pdu; 
    Octstr *os; 
 
    if (date_universal_now() - *last_sent < smpp->enquire_link_interval)
        return; 
    *last_sent = date_universal_now(); 
 
    pdu = smpp_pdu_create(enquire_link, counter_increase(smpp->message_id_counter)); 
    dump_pdu("Sending enquire link:", smpp->conn->id, pdu); 
    os = smpp_pdu_pack(pdu); 
    if (os)
	conn_write(conn, os); /* Write errors checked by caller. */ 
    octstr_destroy(os); 
    smpp_pdu_destroy(pdu); 
} 
 
 
static void send_unbind(SMPP *smpp, Connection *conn)
{
    SMPP_PDU *pdu;
    Octstr *os;
    pdu = smpp_pdu_create(unbind, counter_increase(smpp->message_id_counter));
    dump_pdu("Sending unbind:", smpp->conn->id, pdu);
    os = smpp_pdu_pack(pdu);
    conn_write(conn, os);
    octstr_destroy(os);
    smpp_pdu_destroy(pdu);
}


static int send_pdu(Connection *conn, Octstr *id, SMPP_PDU *pdu) 
{ 
    Octstr *os; 
    int ret; 
     
    dump_pdu("Sending PDU:", id, pdu); 
    os = smpp_pdu_pack(pdu); 
    if (os)
        ret = conn_write(conn, os);   /* Caller checks for write errors later */ 
    else
	ret = -1;
    octstr_destroy(os); 
    return ret; 
} 
 
 
static void send_messages(SMPP *smpp, Connection *conn, long *pending_submits) 
{ 
    Msg *msg; 
    SMPP_PDU *pdu; 
    Octstr *os; 
    double delay = 0;

    if (*pending_submits == -1) 
        return; 

    if (smpp->conn->throughput) {
        delay = 1.0 / smpp->conn->throughput;
    }
 
    while (*pending_submits < smpp->max_pending_submits) {
    	/* Get next message, quit if none to be sent */ 
    	msg = list_extract_first(smpp->msgs_to_send); 
        if (msg == NULL) 
            break; 
	     
        /* Send PDU, record it as waiting for ack from SMS center */ 
        pdu = msg_to_pdu(smpp, msg); 
        os = octstr_format("%ld", pdu->u.submit_sm.sequence_number); 
        dict_put(smpp->sent_msgs, os, msg); 
        octstr_destroy(os); 
        send_pdu(conn, smpp->conn->id, pdu); 
        smpp_pdu_destroy(pdu); 
 
        /* obey throughput speed limit, if any */
        if (smpp->conn->throughput)
            gwthread_sleep(delay);

        ++(*pending_submits); 
    } 
} 
 
 
/* 
 * Open transmission connection to SMS center. Return NULL for error,  
 * open Connection for OK. Caller must set smpp->conn->status correctly  
 * before calling this. 
 */ 
static Connection *open_transmitter(SMPP *smpp) 
{ 
    SMPP_PDU *bind; 
    Connection *conn; 
 
    conn = conn_open_tcp(smpp->host, smpp->transmit_port, smpp->conn->our_host ); 
    if (conn == NULL) { 
        error(0, "SMPP[%s]: Couldn't connect to server.",
              octstr_get_cstr(smpp->conn->id)); 
        return NULL; 
    } 
     
    bind = smpp_pdu_create(bind_transmitter, 
                counter_increase(smpp->message_id_counter)); 
    bind->u.bind_transmitter.system_id = octstr_duplicate(smpp->username); 
    bind->u.bind_transmitter.password = octstr_duplicate(smpp->password); 
    if (smpp->system_type == NULL) 
        bind->u.bind_transmitter.system_type = octstr_create("VMA"); 
    else 
        bind->u.bind_transmitter.system_type =  
	       octstr_duplicate(smpp->system_type); 
    bind->u.bind_transmitter.interface_version = smpp->version;
    bind->u.bind_transmitter.address_range =  
    	octstr_duplicate(smpp->address_range); 
    send_pdu(conn, smpp->conn->id, bind); 
    smpp_pdu_destroy(bind); 
 
    return conn; 
} 
 
 
/* 
 * Open transceiver connection to SMS center. Return NULL for error, 
 * open Connection for OK. Caller must set smpp->conn->status correctly 
 * before calling this. 
 */ 
static Connection *open_transceiver(SMPP *smpp) 
{ 
    SMPP_PDU *bind; 
    Connection *conn; 
     
    conn = conn_open_tcp(smpp->host, smpp->transmit_port, smpp->conn->our_host ); 
    if (conn == NULL) {  
       error(0, "SMPP[%s]: Couldn't connect to server.",
             octstr_get_cstr(smpp->conn->id)); 
       return NULL; 
    } 
 
    bind = smpp_pdu_create(bind_transceiver, 
                           counter_increase(smpp->message_id_counter)); 
    bind->u.bind_transmitter.system_id = octstr_duplicate(smpp->username); 
    bind->u.bind_transmitter.password = octstr_duplicate(smpp->password); 
    if (smpp->system_type == NULL) 
        bind->u.bind_transmitter.system_type = octstr_create("VMA"); 
    else    
        bind->u.bind_transmitter.system_type = octstr_duplicate(smpp->system_type); 
    bind->u.bind_transmitter.interface_version = smpp->version;
    bind->u.bind_transmitter.address_range = octstr_duplicate(smpp->address_range); 
    send_pdu(conn, smpp->conn->id, bind); 
    smpp_pdu_destroy(bind); 
 
    return conn; 
} 
 
 
/* 
 * Open reception connection to SMS center. Return NULL for error,  
 * open Connection for OK. Caller must set smpp->conn->status correctly  
 * before calling this. 
 */ 
static Connection *open_receiver(SMPP *smpp) 
{ 
    SMPP_PDU *bind; 
    Connection *conn; 
 
    conn = conn_open_tcp(smpp->host, smpp->receive_port, smpp->conn->our_host ); 
    if (conn == NULL) { 
        error(0, "SMPP[%s]: Couldn't connect to server.",
              octstr_get_cstr(smpp->conn->id)); 
        return NULL; 
    } 
     
    bind = smpp_pdu_create(bind_receiver, 
                counter_increase(smpp->message_id_counter)); 
    bind->u.bind_receiver.system_id = octstr_duplicate(smpp->username); 
    bind->u.bind_receiver.password = octstr_duplicate(smpp->password); 
    if (smpp->system_type == NULL) 
        bind->u.bind_receiver.system_type = octstr_create("VMA"); 
    else 
        bind->u.bind_receiver.system_type =  
            octstr_duplicate(smpp->system_type); 
    bind->u.bind_receiver.interface_version = smpp->version;
    bind->u.bind_receiver.address_range =  
        octstr_duplicate(smpp->address_range); 
    send_pdu(conn, smpp->conn->id, bind); 
    smpp_pdu_destroy(bind); 
 
    return conn; 
} 
 
 
static void handle_pdu(SMPP *smpp, Connection *conn, SMPP_PDU *pdu,  
    	    	       long *pending_submits) 
{ 
    SMPP_PDU *resp; 
    Octstr *os; 
    Msg *msg, *dlrmsg = NULL; 
    long reason; 
    int cmd_stat;

    resp = NULL;
 
    switch (pdu->type) { 
        case deliver_sm: 
	        /* XXX UDH */ 
	    /*
             * If SMSCConn stopped then send temp. error code
	     */
	     mutex_lock(smpp->conn->flow_mutex);
	     if (smpp->conn->is_stopped) {
                 mutex_unlock(smpp->conn->flow_mutex);
                 resp = smpp_pdu_create(deliver_sm_resp,
                               pdu->u.deliver_sm.sequence_number);
                 resp->u.deliver_sm.command_status = SMPP_ESME_RX_T_APPN;
	         break;
             }
             mutex_unlock(smpp->conn->flow_mutex);

            /* 
             * bb_smscconn_receive can fail, but we ignore that since we 
             * have no way to usefull tell the SMS center about this 
             * (no suitable error code for the deliver_sm_resp is defined) 
             */ 

            /* got a deliver ack (DLR)? 
	     * NOTE: following SMPP v3.4. spec. we are interested
	     *       only on bits 2-5 (some SMSC's send 0x44, and it's
	     *       spec. conforme)
	     * XXX: what is 0x02 for ???
	     */
            if ((pdu->u.deliver_sm.esm_class == 0x02 || 
                 (pdu->u.deliver_sm.esm_class & ~0xC3) == 0x04)) { 
                Octstr *respstr;    	 
                Octstr *msgid = NULL; 
                Octstr *stat = NULL; 
                int dlrstat; 
                long curr = 0, vpos = 0; 
     		 
                debug("bb.sms.smpp",0,"SMPP[%s] handle_pdu, got DLR",
                      octstr_get_cstr(smpp->conn->id)); 
     					 
                respstr = pdu->u.deliver_sm.short_message; 
 		 
                /* get server message id */ 
                if ((curr = octstr_search(respstr, octstr_imm("id:"), 0)) != -1) {    
                    vpos = octstr_search_char(respstr, ' ', curr); 
                    if ((vpos-curr >0) && (vpos != -1)) 
                        msgid = octstr_copy(respstr, curr+3, vpos-curr-3); 
                } else { 
                    msgid = NULL; 
                }
  		 
                /* get err & status code */ 
                if ((curr = octstr_search(respstr, octstr_imm("stat:"), 0)) != -1) {   
                    vpos = octstr_search_char(respstr, ' ', curr); 
                    if ((vpos-curr >0) && (vpos != -1)) 
                        stat = octstr_copy(respstr, curr+5, vpos-curr-5); 
                } else { 
                    stat = NULL; 
                }
	 
                /* 
                 * we get the following status: 
                 * DELIVRD, ACCEPTD, EXPIRED, DELETED, UNDELIV, UNKNOWN, REJECTD 
                 */ 
 		 
                if ((stat != NULL) && ((octstr_compare(stat, octstr_imm("DELIVRD")) == 0) || 
                    (octstr_compare(stat, octstr_imm("ACCEPTD")) == 0))) 
                    dlrstat = DLR_SUCCESS; 
                else 
                    dlrstat = DLR_FAIL; 
 			 
                if (msgid != NULL) { 
                    Octstr *tmp;
                    
                    /* 
                     * Obey which SMPP msg_id type this SMSC is using, where we 
                     * have the following semantics for the variable smpp_msg_id:
                     *
                     * bit 1: type for submit_sm_resp, bit 2: type for deliver_sm 
                     *
                     * if bit is set value is hex otherwise dec
                     *
                     * 0x00 deliver_sm dec, submit_sm_resp dec
                     * 0x01 deliver_sm dec, submit_sm_resp hex
                     * 0x02 deliver_sm hex, submit_sm_resp dec
                     * 0x03 deliver_sm hex, submit_sm_resp hex 
                     *
                     * Default behaviour is SMPP spec compliant, which means
                     * msg_ids should be C strings and hence non modified.
                     */
                    if (smpp->smpp_msg_id_type == -1) {
                        /* the default, C string */
                        tmp = octstr_duplicate(msgid);
                    } else {
                        if (smpp->smpp_msg_id_type & 0x02) {                         
                            tmp = octstr_format("%ld", strtol(octstr_get_cstr(msgid), NULL, 16));
                        } else {
                            tmp = octstr_format("%ld", strtol(octstr_get_cstr(msgid), NULL, 10));
                        }
                    }
 
                    dlrmsg = dlr_find(smpp->conn->id,
                                      tmp, /* smsc message id */
                                      pdu->u.deliver_sm.destination_addr, /* destination */
                                      dlrstat);
                    octstr_destroy(tmp); 
                } 
                if (dlrmsg != NULL) {
                    /* 
                     * we found the delivery report in our storage, so recode the 
                     * message structure. 
                     * The DLR trigger URL is indicated by msg->sms.dlr_url. 
                     */
                    dlrmsg->sms.msgdata = octstr_duplicate(respstr);
                    dlrmsg->sms.sms_type = report;

                    bb_smscconn_receive(smpp->conn, dlrmsg);
                } else { 
                    error(0,"SMPP[%s]: got DLR but could not find message or was not interested in it",
                          octstr_get_cstr(smpp->conn->id));    	 
                }		 
                resp = smpp_pdu_create(deliver_sm_resp,  
                pdu->u.deliver_sm.sequence_number); 
 					        
                if (msgid != NULL) 
                    octstr_destroy(msgid);	     
                if (stat != NULL) 
                    octstr_destroy(stat); 
 	     
            } else /* MO-SMS */
            {
                /* ensure the smsc-id is set */ 
                if ((msg = pdu_to_msg(smpp, pdu)) != NULL) {
                    /* Replace MO destination number with my-number */ 
                    if (octstr_len(smpp->my_number)) { 
                        octstr_destroy(msg->sms.receiver); 
                        msg->sms.receiver = octstr_duplicate(smpp->my_number); 
                    } 

                    time(&msg->sms.time); 
                    msg->sms.smsc_id = octstr_duplicate(smpp->conn->id); 
                    (void) bb_smscconn_receive(smpp->conn, msg); 
                }
                resp = smpp_pdu_create(deliver_sm_resp,  
                            pdu->u.deliver_sm.sequence_number); 
            } 
            break; 
	 
        case enquire_link: 
            resp = smpp_pdu_create(enquire_link_resp,  
                        pdu->u.enquire_link.sequence_number); 
            break; 
 
        case enquire_link_resp: 
            break; 
 
        case submit_sm_resp: 
            os = octstr_format("%ld", pdu->u.submit_sm_resp.sequence_number); 
            msg = dict_remove(smpp->sent_msgs, os); 
            octstr_destroy(os); 
            if (msg == NULL) { 
                warning(0, "SMPP[%s]: SMSC sent submit_sm_resp " 
                        "with wrong sequence number 0x%08lx",
                        octstr_get_cstr(smpp->conn->id),
                        pdu->u.submit_sm_resp.sequence_number); 
            } else if (pdu->u.submit_sm_resp.command_status != 0) { 
                error(0, "SMPP[%s]: SMSC returned error code 0x%08lx (%s) " 
                      "in response to submit_sm.",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.submit_sm_resp.command_status,
		      smpp_error_to_string(pdu->u.submit_sm_resp.command_status)); 
                reason = smpp_status_to_smscconn_failure_reason( 
                            pdu->u.submit_sm_resp.command_status); 

                /* 
                 * check to see if we got a "throttling error", in which case we'll just
                 * sleep for a while 
                 */
                if (pdu->u.submit_sm_resp.command_status == SMPP_ESME_RTHROTTLED)
                    time(&(smpp->throttling_err_time));
                else
                    smpp->throttling_err_time = 0;
 
                /* gen DLR_SMSC_FAIL */		 
                if (reason == SMSCCONN_FAILED_REJECTED &&
                    (msg->sms.dlr_mask & (DLR_SMSC_FAIL|DLR_FAIL))) { 
                    Octstr *reply; 
 		 
                    reply = octstr_format("0x%08lx", 
                                pdu->u.submit_sm_resp.command_status); 
                    /* generate DLR */ 
                    info(0,"SMPP[%s]: creating DLR message",
                         octstr_get_cstr(smpp->conn->id)); 
                    dlrmsg = msg_create(sms); 
                    dlrmsg->sms.service = octstr_duplicate(msg->sms.service); 
                    dlrmsg->sms.dlr_mask = DLR_SMSC_FAIL; 
                    dlrmsg->sms.sms_type = report; 
                    dlrmsg->sms.smsc_id = octstr_duplicate(smpp->conn->id); 
                    dlrmsg->sms.sender = octstr_duplicate(msg->sms.receiver); 
                    dlrmsg->sms.receiver = octstr_create("000"); 
                    dlrmsg->sms.dlr_url = octstr_duplicate(msg->sms.dlr_url);

                    dlrmsg->sms.msgdata = reply;

                    time(&dlrmsg->sms.time); 
 			 
                    info(0,"SMPP[%s]: DLR = %s", octstr_get_cstr(smpp->conn->id),
                         octstr_get_cstr(dlrmsg->sms.dlr_url)); 
                    bb_smscconn_receive(smpp->conn, dlrmsg); 
                } 

                bb_smscconn_send_failed(smpp->conn, msg, reason);
                --(*pending_submits); 
            } else {  
                Octstr *tmp; 
	 
                /* check if msg_id is C string, decimal or hex for this SMSC */
                if (smpp->smpp_msg_id_type == -1) {
                    /* the default, C string */
                    tmp = octstr_duplicate(pdu->u.submit_sm_resp.message_id);
                } else {
                    if (smpp->smpp_msg_id_type & 0x01) {   
                        tmp = octstr_format("%ld", strtol(  /* hex */
                            octstr_get_cstr(pdu->u.submit_sm_resp.message_id), NULL, 16));
                    } else {
                        tmp = octstr_format("%ld", strtol(  /* decimal */
                            octstr_get_cstr(pdu->u.submit_sm_resp.message_id), NULL, 10));
                    }
                }
 
                /* SMSC ACK.. now we have the message id. */ 
 				 
                if (msg->sms.dlr_mask & (DLR_SMSC_SUCCESS|DLR_SUCCESS|DLR_FAIL|DLR_BUFFERED)) 
                    dlr_add(smpp->conn->id, tmp, msg);
  
                /* gen DLR_SMSC_SUCCESS */ 
                if (msg->sms.dlr_mask & DLR_SMSC_SUCCESS) { 
                    Octstr *reply; 
 		 
                    reply = octstr_format("0x%08lx", pdu->u.submit_sm_resp.command_status); 
  
                    dlrmsg = dlr_find(smpp->conn->id,
                                      tmp, /* smsc message id */
                                      msg->sms.receiver, /* destination */ 
                                      (DLR_SMSC_SUCCESS|((msg->sms.dlr_mask & (DLR_SUCCESS|DLR_FAIL)) ? DLR_BUFFERED : 0))); 
 			 
                    if (dlrmsg != NULL) { 
                        octstr_append_char(reply, '/'); 
                        dlrmsg->sms.msgdata = octstr_duplicate(reply);
                        octstr_destroy(reply); 
                        bb_smscconn_receive(smpp->conn, dlrmsg); 
                    } else 
                        error(0,"SMPP[%s]: Got SMSC_ACK but could not find message",
                              octstr_get_cstr(smpp->conn->id)); 
                } 
                octstr_destroy(tmp); 
                bb_smscconn_sent(smpp->conn, msg); 
                --(*pending_submits); 
            } /* end if for SMSC ACK */ 
            break; 
 
        case bind_transmitter_resp: 
            if (pdu->u.bind_transmitter_resp.command_status != 0) { 
                error(0, "SMPP[%s]: SMSC rejected login to transmit, " 
		              "code 0x%08lx (%s).",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.bind_transmitter_resp.command_status,
		      smpp_error_to_string(pdu->u.bind_transmitter_resp.command_status)); 
                if (pdu->u.bind_transmitter_resp.command_status == SMPP_ESME_RINVSYSID ||
                    pdu->u.bind_transmitter_resp.command_status == SMPP_ESME_RINVPASWD)
                    smpp->quitting = 1;
            } else { 
                *pending_submits = 0; 
                smpp->conn->status = SMSCCONN_ACTIVE; 
                smpp->conn->connect_time = time(NULL); 
                bb_smscconn_connected(smpp->conn); 
            } 
            break; 
 
        case bind_transceiver_resp: 
            if (pdu->u.bind_transceiver_resp.command_status != 0) { 
                error(0, "SMPP[%s]: SMSC rejected login to transmit, " 
                      "code 0x%08lx (%s).",
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.bind_transceiver_resp.command_status,
		      smpp_error_to_string(pdu->u.bind_transceiver_resp.command_status)); 
                if (pdu->u.bind_transceiver_resp.command_status == SMPP_ESME_RINVSYSID ||
                    pdu->u.bind_transceiver_resp.command_status == SMPP_ESME_RINVPASWD)
                    smpp->quitting = 1;
            } else { 
                *pending_submits = 0; 
                smpp->conn->status = SMSCCONN_ACTIVE; 
                smpp->conn->connect_time = time(NULL); 
                bb_smscconn_connected(smpp->conn); 
            } 
            break; 
 
        case bind_receiver_resp: 
            if (pdu->u.bind_receiver_resp.command_status != 0) { 
                error(0, "SMPP[%s]: SMSC rejected login to receive, " 
                      "code 0x%08lx (%s).", 
                      octstr_get_cstr(smpp->conn->id),
                      pdu->u.bind_receiver_resp.command_status,
		      smpp_error_to_string(pdu->u.bind_receiver_resp.command_status)); 
                if (pdu->u.bind_receiver_resp.command_status == SMPP_ESME_RINVSYSID ||
                    pdu->u.bind_receiver_resp.command_status == SMPP_ESME_RINVPASWD)
                    smpp->quitting = 1;
            } else { 
                /* set only resceive status if no transmitt is bind */
                if (smpp->conn->status != SMSCCONN_ACTIVE) {
                    smpp->conn->status = SMSCCONN_ACTIVE_RECV; 
                    smpp->conn->connect_time = time(NULL);
                }
            } 
            break; 
 
        case unbind:
            break;          
 
        case unbind_resp:       
            break;

        case generic_nack:
            cmd_stat  = pdu->u.generic_nack.command_status;

            os = octstr_format("%ld", pdu->u.generic_nack.sequence_number);
            msg = dict_remove(smpp->sent_msgs, os);
            octstr_destroy(os);

            if (msg == NULL) {
                warning(0, "SMPP[%s]: SMSC sent generic_nack "
                        "with wrong sequence number 0x%08lx",
                        octstr_get_cstr(smpp->conn->id),
                        pdu->u.generic_nack.sequence_number);
            } else {
                if ((cmd_stat == SMPP_ESME_RTHROTTLED) ||
                    (cmd_stat == SMPP_ESME_RMSGQFUL)) {
                    info(0, "SMPP[%s]: SMSC sent generic_nack %s: status 0x%08lx ",
                        (cmd_stat == SMPP_ESME_RTHROTTLED ? 
                            "ESME_RTHROTTLED" : "ESME_RMSGQFUL"),
                        octstr_get_cstr(smpp->conn->id),
                        pdu->u.generic_nack.command_status);

                    time(&(smpp->throttling_err_time));
                    reason = smpp_status_to_smscconn_failure_reason(
                                pdu->u.generic_nack.command_status);
                    bb_smscconn_send_failed(smpp->conn, msg, reason);
                    --(*pending_submits);
                } else if (cmd_stat == SMPP_ESME_RUNKNOWNERR) {
                    info(0, "SMPP[%s]: SMSC sent generic_nack SMPP_ESME_RUNKNOWNERR: status 0x%08lx ",
                         octstr_get_cstr(smpp->conn->id),
                         pdu->u.generic_nack.command_status);
                    reason = smpp_status_to_smscconn_failure_reason(-1);
                    bb_smscconn_send_failed(smpp->conn, msg, reason);
                    --(*pending_submits);
                } else {
                    error(0, "SMPP[%s]: SMSC sent generic_nack type 0x%08lx, code 0x%08lx (%s).",
                          octstr_get_cstr(smpp->conn->id), pdu->type,
                          pdu->u.generic_nack.command_status,
			  smpp_error_to_string(pdu->u.generic_nack.command_status));
                    reason = smpp_status_to_smscconn_failure_reason(-1);
                    bb_smscconn_send_failed(smpp->conn, msg, reason);
                    --(*pending_submits);
                }
            }
            break;
        default: 
            error(0, "SMPP[%s]: Unknown PDU type 0x%08lx, ignored.",
                  octstr_get_cstr(smpp->conn->id), pdu->type); 
            break; 
    } 
     
    if (resp != NULL) { 
    	send_pdu(conn, smpp->conn->id, resp); 
        smpp_pdu_destroy(resp); 
    } 
} 
 
 
struct io_arg { 
    SMPP *smpp; 
    int transmitter; 
}; 
 
 
static struct io_arg *io_arg_create(SMPP *smpp, int transmitter) 
{ 
    struct io_arg *io_arg; 
     
    io_arg = gw_malloc(sizeof(*io_arg)); 
    io_arg->smpp = smpp; 
    io_arg->transmitter = transmitter; 
    return io_arg; 
} 
 
 
 
/* 
 * This is the main function for the background thread for doing I/O on 
 * one SMPP connection (the one for transmitting or receiving messages). 
 * It makes the initial connection to the SMPP server and re-connects 
 * if there are I/O errors or other errors that require it. 
 */ 
static void io_thread(void *arg) 
{ 
    SMPP *smpp; 
    struct io_arg *io_arg; 
    int transmitter; 
    Connection *conn; 
    int ret; 
    long last_enquire_sent; 
    long pending_submits; 
    long len; 
    SMPP_PDU *pdu; 
    double timeout; 
 
    io_arg = arg; 
    smpp = io_arg->smpp; 
    transmitter = io_arg->transmitter; 
    gw_free(io_arg); 

    /* Make sure we log into our own log-file if defined */
    log_thread_to(smpp->conn->log_idx);
 
    conn = NULL; 
    while (!smpp->quitting) { 
        if (transmitter == 1) 
            conn = open_transmitter(smpp); 
        else if (transmitter == 2) 
            conn = open_transceiver(smpp); 
        else 
            conn = open_receiver(smpp); 
        if (conn == NULL) { 
            error(0, "SMPP[%s]: Couldn't connect to SMS center (retrying in %ld seconds).",
                  octstr_get_cstr(smpp->conn->id), smpp->conn->reconnect_delay);
            gwthread_sleep(smpp->conn->reconnect_delay);
            smpp->conn->status = SMSCCONN_RECONNECTING; 
            continue; 
        } 
	 
        last_enquire_sent = date_universal_now(); 
        pending_submits = -1; 
        len = 0; 
        for (;;) { 
            timeout = last_enquire_sent + smpp->enquire_link_interval
                        - date_universal_now(); 

            /* unbind */
            if (smpp->quitting) {
                send_unbind(smpp, conn);
                while ((ret = read_pdu(smpp, conn, &len, &pdu)) == 1) {
                    dump_pdu("Got PDU:", smpp->conn->id, pdu);
                    handle_pdu(smpp, conn, pdu, &pending_submits);
                    smpp_pdu_destroy(pdu);
                }
                debug("bb.sms.smpp", 0, "SMPP[%s]: %s: break and shutting down",
                      octstr_get_cstr(smpp->conn->id), __PRETTY_FUNCTION__);
            }

            if (smpp->quitting || conn_wait(conn, timeout) == -1) 
                break; 
 
            send_enquire_link(smpp, conn, &last_enquire_sent); 
	     
            while ((ret = read_pdu(smpp, conn, &len, &pdu)) == 1) { 
                /* Deal with the PDU we just got */ 
                dump_pdu("Got PDU:", smpp->conn->id, pdu); 
                handle_pdu(smpp, conn, pdu, &pending_submits); 
                smpp_pdu_destroy(pdu); 
 
                /* Make sure we send enquire_link even if we read a lot */ 
                send_enquire_link(smpp, conn, &last_enquire_sent); 
 
                /* Make sure we send even if we read a lot */ 
                if (transmitter &&
                    (!smpp->throttling_err_time || 
                    ((time(NULL) - smpp->throttling_err_time) > SMPP_THROTTLING_SLEEP_TIME 
                        && !(smpp->throttling_err_time = 0)))
                    )
                    send_messages(smpp, conn, &pending_submits); 
            } 
	     
            if (ret == -1) { 
                error(0, "SMPP[%s]: I/O error or other error. Re-connecting.",
                      octstr_get_cstr(smpp->conn->id)); 
                break; 
            } 
	     
            if (transmitter &&
                (!smpp->throttling_err_time || 
                ((time(NULL) - smpp->throttling_err_time) > SMPP_THROTTLING_SLEEP_TIME 
                    && !(smpp->throttling_err_time = 0)))
                )
                send_messages(smpp, conn, &pending_submits); 
        } 
	 
        conn_destroy(conn); 
        conn = NULL; 
    } 
    conn_destroy(conn); 
} 
     
 
/*********************************************************************** 
 * Functions called by smscconn.c via the SMSCConn function pointers. 
 */ 
  
 
static long queued_cb(SMSCConn *conn) 
{ 
    SMPP *smpp; 
 
    smpp = conn->data;
    conn->load = (smpp ? (conn->status != SMSCCONN_DEAD ? 
                  list_len(smpp->msgs_to_send) : 0) : 0);
    return conn->load; 
} 
 
 
static int send_msg_cb(SMSCConn *conn, Msg *msg) 
{ 
    SMPP *smpp; 
     
    smpp = conn->data; 
    list_produce(smpp->msgs_to_send, msg_duplicate(msg)); 
    gwthread_wakeup(smpp->transmitter); 
    return 0; 
} 
 
 
static int shutdown_cb(SMSCConn *conn, int finish_sending) 
{ 
    SMPP *smpp; 
 
    debug("bb.smpp", 0, "Shutting down SMSCConn %s (%s)", 
          octstr_get_cstr(conn->name), 
          finish_sending ? "slow" : "instant"); 
 
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN; 
 
    /* XXX implement finish_sending */ 
 
    smpp = conn->data; 
    smpp->quitting = 1; 
    if (smpp->transmitter != -1) { 
        gwthread_wakeup(smpp->transmitter); 
        gwthread_join(smpp->transmitter); 
    } 
    if (smpp->receiver != -1) { 
        gwthread_wakeup(smpp->receiver); 
        gwthread_join(smpp->receiver); 
    } 
    smpp_destroy(smpp); 
     
    debug("bb.smpp", 0, "SMSCConn %s shut down.",  
          octstr_get_cstr(conn->name)); 
    conn->status = SMSCCONN_DEAD; 
    bb_smscconn_killed(); 
    return 0; 
} 
 
 
/*********************************************************************** 
 * Public interface. This version is suitable for the Kannel bearerbox 
 * SMSCConn interface. 
 */ 
 
 
int smsc_smpp_create(SMSCConn *conn, CfgGroup *grp) 
{ 
    Octstr *host; 
    long port; 
    long receive_port; 
    Octstr *username; 
    Octstr *password; 
    Octstr *system_id; 
    Octstr *system_type; 
    Octstr *address_range; 
    long source_addr_ton; 
    long source_addr_npi; 
    long dest_addr_ton; 
    long dest_addr_npi; 
    Octstr *my_number; 
    Octstr *service_type;
    SMPP *smpp; 
    int ok; 
    int transceiver_mode; 
    Octstr *smsc_id; 
    long enquire_link_interval;
    long max_pending_submits;
    long version;
    long priority;
    long smpp_msg_id_type;
    int autodetect_addr;
    Octstr *alt_charset;
 
    my_number = alt_charset = NULL; 
    transceiver_mode = 0;
    autodetect_addr = 1;
 
    host = cfg_get(grp, octstr_imm("host")); 
    if (cfg_get_integer(&port, grp, octstr_imm("port")) == -1) 
        port = 0; 
    if (cfg_get_integer(&receive_port, grp, octstr_imm("receive-port")) == -1) 
        receive_port = 0; 
    cfg_get_bool(&transceiver_mode, grp, octstr_imm("transceiver-mode")); 
    username = cfg_get(grp, octstr_imm("smsc-username")); 
    password = cfg_get(grp, octstr_imm("smsc-password")); 
    system_type = cfg_get(grp, octstr_imm("system-type")); 
    address_range = cfg_get(grp, octstr_imm("address-range")); 
    my_number = cfg_get(grp, octstr_imm("my-number")); 
    service_type = cfg_get(grp, octstr_imm("service-type")); 
     
    system_id = cfg_get(grp, octstr_imm("system-id")); 
    if (system_id != NULL) { 
        warning(0, "SMPP: obsolete system-id variable is set, " 
	    	   "use smsc-username instead."); 
        if (username == NULL) { 
            warning(0, "SMPP: smsc-username not set, using system-id instead"); 
            username = system_id; 
        } else 
            octstr_destroy(system_id); 
    } 

    /* 
     * check if timing values have been configured, otherwise
     * use the predefined default values.
     */
    if (cfg_get_integer(&enquire_link_interval, grp, 
                        octstr_imm("enquire-link-interval")) == -1)
        enquire_link_interval = SMPP_ENQUIRE_LINK_INTERVAL;
    if (cfg_get_integer(&max_pending_submits, grp, 
                        octstr_imm("max-pending-submits")) == -1)
        max_pending_submits = SMPP_MAX_PENDING_SUBMITS;
 
    /* Check that config is OK */ 
    ok = 1; 
    if (host == NULL) { 
        error(0,"SMPP: Configuration file doesn't specify host"); 
        ok = 0; 
    }     
    if (username == NULL) { 
	    error(0, "SMPP: Configuration file doesn't specify username."); 
	    ok = 0; 
    } 
    if (password == NULL) { 
	    error(0, "SMPP: Configuration file doesn't specify password."); 
	    ok = 0; 
    } 
    if (system_type == NULL) { 
	    error(0, "SMPP: Configuration file doesn't specify system-type."); 
	    ok = 0; 
    } 
    if (octstr_len(service_type) > 6) {
            error(0, "SMPP: Service type must be 6 characters or less.");
            ok = 0;
    }

    if (!ok) 
        return -1; 
 
    /* if the ton and npi values are forced, set them, else set them to -1 */ 
    if (cfg_get_integer(&source_addr_ton, grp, 
                        octstr_imm("source-addr-ton")) == -1) 
        source_addr_ton = -1; 
    if (cfg_get_integer(&source_addr_npi, grp, 
                        octstr_imm("source-addr-npi")) == -1) 
        source_addr_npi = -1; 
    if (cfg_get_integer(&dest_addr_ton, grp, 
                        octstr_imm("dest-addr-ton")) == -1) 
        dest_addr_ton = -1; 
    if (cfg_get_integer(&dest_addr_npi, grp, 
                        octstr_imm("dest-addr-npi")) == -1) 
        dest_addr_npi = -1; 

    /* if source addr autodetection should be used set this to 1 */
    cfg_get_bool(&autodetect_addr, grp, octstr_imm("source-addr-autodetect")); 

    /* check for any specified interface version */
    if (cfg_get_integer(&version, grp, octstr_imm("interface-version")) == -1)
        version = SMPP_DEFAULT_VERSION;
    else
        /* convert decimal to BCD */
        version = ((version / 10) << 4) + (version % 10);

    /* check for any specified priority value in range [0-5] */
    if (cfg_get_integer(&priority, grp, octstr_imm("priority")) == -1)
        priority = SMPP_DEFAULT_PRIORITY;

    /* set the msg_id type variable for this SMSC */
    if (cfg_get_integer(&smpp_msg_id_type, grp, octstr_imm("msg-id-type")) == -1) {
        /* 
         * defaults to C string "as-is" style 
         */
        smpp_msg_id_type = -1; 
    } else {
        if (smpp_msg_id_type < 0 || smpp_msg_id_type > 3)
            panic(0,"SMPP: Invalid value for msg-id-type directive in configuraton"); 
    }

    /* check for an alternative charset */
    alt_charset = cfg_get(grp, octstr_imm("alt-charset"));

    smpp = smpp_create(conn, host, port, receive_port, system_type,  
    	    	       username, password, address_range,
                       source_addr_ton, source_addr_npi, dest_addr_ton,  
                       dest_addr_npi, enquire_link_interval, 
                       max_pending_submits, version, priority, my_number, 
                       smpp_msg_id_type, autodetect_addr, alt_charset, 
                       service_type); 
 
    conn->data = smpp; 
    conn->name = octstr_format("SMPP:%S:%d/%d:%S:%S",  
    	    	    	       host, port, 
                               (receive_port ? receive_port : port),  
                               username, system_type); 
 
    smsc_id = cfg_get(grp, octstr_imm("smsc-id")); 
    if (smsc_id == NULL) { 
        conn->id = octstr_duplicate(conn->name); 
    } 

    octstr_destroy(host); 
    octstr_destroy(username); 
    octstr_destroy(password); 
    octstr_destroy(system_type); 
    octstr_destroy(address_range); 
    octstr_destroy(my_number); 
    octstr_destroy(smsc_id);
    octstr_destroy(alt_charset); 
    octstr_destroy(service_type);

    conn->status = SMSCCONN_CONNECTING; 
       
    /* 
     * I/O threads are only started if the corresponding ports 
     * have been configured with positive numbers. Use 0 to  
     * disable the creation of the corresponding thread. 
     */ 
    if (port != 0) 
        smpp->transmitter = gwthread_create(io_thread, io_arg_create(smpp,  
                                           (transceiver_mode ? 2 : 1))); 
    if (receive_port != 0) 
        smpp->receiver = gwthread_create(io_thread, io_arg_create(smpp, 0)); 
     
    if ((port != 0 && smpp->transmitter == -1) ||  
        (receive_port != 0 && smpp->receiver == -1)) { 
        error(0, "SMPP[%s]: Couldn't start I/O threads.",
              octstr_get_cstr(smpp->conn->id)); 
        smpp->quitting = 1; 
        if (smpp->transmitter != -1) { 
            gwthread_wakeup(smpp->transmitter); 
            gwthread_join(smpp->transmitter); 
        } 
        if (smpp->receiver != -1) { 
            gwthread_wakeup(smpp->receiver); 
            gwthread_join(smpp->receiver); 
        } 
    	smpp_destroy(conn->data); 
        return -1; 
    } 
 
    conn->shutdown = shutdown_cb; 
    conn->queued = queued_cb; 
    conn->send_msg = send_msg_cb; 
 
    return 0; 
}
 
