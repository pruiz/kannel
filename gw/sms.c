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

    /* Coding defaults to 7BIT or to 8BIT if udh is set */
    if (msg->sms.coding == DC_UNDEF) {
	if (octstr_len(msg->sms.udhdata))
	  msg->sms.coding = DC_8BIT;
	else
	  msg->sms.coding = DC_7BIT;
    }


    /* MWI */
    if (msg->sms.mwi != MWI_UNDEF) {
	dcs = msg->sms.mwi - 1;  /* sets bits 1 and 0 */

	if (dcs & 0x04)	
	    dcs = (dcs & 0x03) | 0xC0; /* MWI Inactive */
	else {
	    dcs = (dcs & 0x03) | 0x08;	/* MWI Active, sets bit 3 */

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
	    /* bits 7,6 are 0 */
	    if (msg->sms.compress)
		dcs |= 0x20; /* sets bit 5 */
	    if (msg->sms.mclass)
		dcs |= 0x10 | (msg->sms.mclass - 1); /* sets bit 4,1,0 */
	    if (msg->sms.coding)
		dcs |= ((msg->sms.coding - 1) << 2); /* sets bit 3,2 */
	} 
	
	/* mode 1 */
	else {
	    dcs |= 0xF0; /* sets bits 7-3 */
	    dcs |= (msg->sms.coding - 1) << 2; /* only DC_7BIT or DC_8BIT, sets bit 2*/
	    if (msg->sms.mclass == 0)
		dcs |= 1; /* sets bit 1,0 */
	    else
		dcs |= (msg->sms.mclass - 1); /* sets bit 1,0 */
	}
    }

    return dcs;
}


/*
 * Decode DCS to sms fields
 */
int dcs_to_fields(Msg **msg, int dcs) {

    /* Non-MWI Mode 1 */
    if ((dcs & 0xF0) == 0xF0) { 
	dcs &= 0x07;
	(*msg)->sms.coding = (dcs & 0x04) ? DC_8BIT : DC_7BIT; /* grab bit 2 */
	(*msg)->sms.mclass = 1 + (dcs & 0x03); /* grab bits 1,0 */
    }
    
    /* Non-MWI Mode 0 */
    else if ((dcs & 0xC0) == 0x00) { 
	(*msg)->sms.compress = ((dcs & 0x20) == 0x20) ? 1 : 0; /* grab bit 5 */
	(*msg)->sms.mclass = ((dcs & 0x10) == 0x10) ? 1 + (dcs & 0x03) : 0; 
	    /* grab bit 0,1 if bit 4 is on */
	(*msg)->sms.coding = 1 + ((dcs & 0x0C) >> 2); /* grab bit 3,2 */
    }

    /* MWI */
    else if ((dcs & 0xC0) == 0xC0) { 
	(*msg)->sms.coding = ((dcs & 0x30) == 0x30) ? DC_UCS2 : DC_7BIT;
	if (dcs & 0x08)
	    dcs |= 0x04; /* if bit 3 is active, have mwi += 4 */
	dcs &= 0x07;
 	(*msg)->sms.mwi = 1 + dcs ; /* grab bits 1,0 */
    } 
    
    else {
	return 0;
    }

    return 1;
}


/*
 * Compute length of an Octstr after it will be converted to GSM 03.38 
 * 7 bit alphabet - escaped characters would be counted as two septets
 */
int sms_msgdata_len(Msg* msg) {

	int ret = 0;
	Octstr* msgdata = NULL;
	
	/* got a bad input */
	if (!msg || !msg->sms.msgdata) 
		return -1;

	if (msg->sms.coding == DC_7BIT) {
		msgdata = octstr_duplicate(msg->sms.msgdata);
		charset_latin1_to_gsm(msgdata);
		ret = octstr_len(msgdata);
		octstr_destroy(msgdata);
	} else 
		ret = octstr_len(msg->sms.msgdata);

	return ret;
}

