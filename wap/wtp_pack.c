/*                          
 * wtp_pack.c - WTP message packing module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "gwlib/gwlib.h"
#include "wtp_pack.h"
#include "wtp_pdu.h"


/*
 * Readable names for octets
 */
enum {
    first_byte,
    second_byte,
    third_byte,
    fourth_byte
};

/*
 * Types of header information added by the user (TPIs, or transportation 
 * information items). 
 */
enum {
    ERROR_DATA = 0x00,
    INFO_DATA = 0x01,
    OPTION = 0x02,
    PACKET_SEQUENCE_NUMBER = 0x03
};

/*****************************************************************************
 *
 * Prototypes of internal functions
 */

/*
 * WTP defines SendTID and RcvTID.  We should use SendTID in all PDUs
 * we send.  The RcvTID is the one we got from the initial Invoke and
 * is the one we expect on all future PDUs for this machine.  
 * SendTID is always RcvTID xor 0x8000.
 * 
 * Note that when we are the Initiator, for example with WSP PUSH,
 * we must still store the RcvTID in machine->tid, to be consistent
 * with the current code.  So we'll choose the SendTID and then calculate
 * the RcvTID.
 */
static unsigned short send_tid(unsigned short tid);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */

WAPEvent *wtp_pack_invoke(WTPInitMachine *machine, WAPEvent *event)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu = NULL;

    gw_assert(event->type == TR_Invoke_Req);
    pdu = wtp_pdu_create(Invoke);
    pdu->u.Invoke.con = 0;
    pdu->u.Invoke.gtr = 1;
    pdu->u.Invoke.ttr = 1;
    pdu->u.Invoke.rid = 0;
    pdu->u.Invoke.version = 0;
/* 
 * Now SendTID = GenTID (See WTP 10.5)
 */
    pdu->u.Invoke.tid = (unsigned short) machine->tid;
    pdu->u.Invoke.tidnew = machine->tidnew;
    pdu->u.Invoke.user_data = octstr_duplicate(
        event->u.TR_Invoke_Req.user_data);
    pdu->u.Invoke.class = event->u.TR_Invoke_Req.tcl;
    pdu->u.Invoke.uack = event->u.TR_Invoke_Req.up_flag;

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple =
	wap_addr_tuple_duplicate(machine->addr_tuple);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}
 
WAPEvent *wtp_pack_result(WTPRespMachine *machine, WAPEvent *event)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu = NULL;
     
    gw_assert(event->type == TR_Result_Req);
    pdu = wtp_pdu_create(Result);
    pdu->u.Result.con = 0;
    pdu->u.Result.gtr = 1;
    pdu->u.Result.ttr = 1;
    pdu->u.Result.rid = 0;
    pdu->u.Result.tid = send_tid(machine->tid);
    pdu->u.Result.user_data = 
     	octstr_duplicate(event->u.TR_Result_Req.user_data);

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple =
	wap_addr_tuple_duplicate(machine->addr_tuple);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);
  
    return dgram;
}

void wtp_pack_set_rid(WAPEvent *dgram, long rid)
{
    gw_assert(dgram != NULL);
    gw_assert(dgram->type == T_DUnitdata_Req);

    octstr_set_bits(dgram->u.T_DUnitdata_Req.user_data, 7, 1, rid);
}

WAPEvent *wtp_pack_abort(long abort_type, long abort_reason, long tid, 
                         WAPAddrTuple *address) 
{
    WAPEvent *dgram;
    WTP_PDU *pdu;

    pdu = wtp_pdu_create(Abort);
    pdu->u.Abort.con = 0;
    pdu->u.Abort.abort_type = abort_type;
    pdu->u.Abort.tid = send_tid(tid);
    pdu->u.Abort.abort_reason = abort_reason;

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple = wap_addr_tuple_duplicate(address);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}

WAPEvent *wtp_pack_ack(long ack_type, int rid_flag, long tid, 
                       WAPAddrTuple *address)
{
    WAPEvent *dgram = NULL;
    WTP_PDU *pdu;
     
    pdu = wtp_pdu_create(Ack);
    pdu->u.Ack.con = 0;
    pdu->u.Ack.tidverify = ack_type;
    pdu->u.Ack.rid = rid_flag;
    pdu->u.Ack.tid = send_tid(tid);

    dgram = wap_event_create(T_DUnitdata_Req);
    dgram->u.T_DUnitdata_Req.addr_tuple = wap_addr_tuple_duplicate(address);
    dgram->u.T_DUnitdata_Req.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    return dgram;
}

/****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 */

static unsigned short send_tid(unsigned short tid)
{
       return tid ^ 0x8000;
}

/****************************************************************************/
