/*
 * sms.c - features specific to SMS but not particular to any SMSC protocol.
 *
 * This file current contains very little, but sms features that are
 * currently implemented separately in each protocol should be extracted
 * and placed here.
 */

#include "sms.h"

/* 
 * Encode DCS using sms fields
 * mode = 0= encode using 00xxx, 1= encode using Fx mode
 *
 */
int fields_to_dcs(Msg *msg, int mode) {
    int dcs=0;

    if (msg->sms.coding == DC_UNDEF)
	msg->sms.coding = DC_7BIT;

    /* MWI */
    if (msg->sms.mwi != MWI_UNDEF) {
	dcs = msg->sms.mwi - 1;

	if (dcs & 0x04)	
	    dcs = (dcs & 0x03) | 0xC0; /* MWI Inactive */
	else {
	    dcs = (dcs & 0x03) | 0x08;	/* MWI Active */

	    if (! octstr_len(msg->sms.msgdata))
		dcs |= 0xC0;	/* Discard */
	    else
		if (msg->sms.coding == DC_7BIT)
		    dcs |= 0xD0;	/* 7bit */
		else
		    dcs |= 0xE0;	/* UCS2 */
	    	/* XXX Shouldn't happen to have mwi and dc=DC_8BIT! */
	}
    }

    /* Non-MWI */
    else {
	/* mode 0 */
	if (mode == 0 || msg->sms.coding == DC_UCS2 || msg->sms.compress) { 
	    if (msg->sms.compress)
		dcs |= 0x20;
	    if (msg->sms.mclass)
		dcs |= 0x10 | (msg->sms.mclass - 1);
	    if (msg->sms.coding)
		dcs |= ((msg->sms.coding - 1) << 2);
	} 
	
	/* mode 1 */
	else {
	    dcs |= 0xF0;
	    dcs |= msg->sms.coding << 2; /* only DC_7BIT or DC_8BIT */
	    dcs |= msg->sms.mclass;
	}
    }
    return dcs;
}


/*
 * Decode DCS to sms fields
 */
int dcs_to_fields(Msg **msg, int dcs) {

    if ((dcs & 0xF0) == 0xF0) { /* Non-MWI Mode 1 */
	dcs &= 0x07;
	(*msg)->sms.coding = (dcs & 0x04) ? DC_8BIT : DC_7BIT;
	(*msg)->sms.mclass = dcs & 0x03;
    }
    
    else if ((dcs & 0xC0) == 0x00) { /* Non-MWI Mode 0 */
	(*msg)->sms.compress = ((dcs & 0x20) == 0x20) ? 1 : 0;
	(*msg)->sms.mclass = ((dcs & 0x10) == 0x10) ? dcs & 0x03 : 0;
	(*msg)->sms.coding = ((dcs & 0x0C) >> 2) + 1;
    }

    else if ((dcs & 0xC0) == 0xC0) { /* MWI */
	(*msg)->sms.coding = ((dcs & 0x30) == 0x30) ? DC_UCS2 : DC_7BIT;
	if (dcs & 0x08)
	    dcs |= 0x04;
	dcs &= 0x07;
 	(*msg)->sms.mwi = dcs + 1;  
    } 
    
    else {
	return 0;
    }

    return 1;
}
