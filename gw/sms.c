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
int fields_to_dcs(Msg *msg, int mode) 
{
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
	dcs = msg->sms.mwi;  /* sets bits 2, 1 and 0 */

	if (dcs & 0x04)	
	    dcs = (dcs & 0x03) | 0xC0; /* MWI Inactive */
	else {
	    dcs = (dcs & 0x03) | 0x08; /* MWI Active, sets bit 3 */

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
	/* mode 0 or mode UNDEF */
	if (mode == 0 || mode == SMS_PARAM_UNDEFINED || msg->sms.coding == DC_UCS2 
	    || msg->sms.compress == COMPRESS_ON) { 
	    /* bits 7,6 are 0 */
	    if (msg->sms.compress == COMPRESS_ON)
		dcs |= 0x20; /* sets bit 5 */
	    if (msg->sms.mclass != MC_UNDEF)
		dcs |= (0x10 | msg->sms.mclass); /* sets bit 4,1,0 */
	    if (msg->sms.coding != DC_UNDEF)
		dcs |= (msg->sms.coding << 2); /* sets bit 3,2 */
	} 
	
	/* mode 1 */
	else {
	    dcs |= 0xF0; /* sets bits 7-3 */
	    if(msg->sms.coding != DC_UNDEF)
		dcs |= (msg->sms.coding << 2); /* only DC_7BIT or DC_8BIT, sets bit 2*/
	    if (msg->sms.mclass == MC_UNDEF)
                dcs |= 1; /* default meaning: ME specific */
            else
                dcs |= msg->sms.mclass; /* sets bit 1,0 */
	}
    }

    return dcs;
}


/*
 * Decode DCS to sms fields
 */
int dcs_to_fields(Msg **msg, int dcs) 
{
    /* Non-MWI Mode 1 */
    if ((dcs & 0xF0) == 0xF0) { 
        dcs &= 0x07;
        (*msg)->sms.coding = (dcs & 0x04) ? DC_8BIT : DC_7BIT; /* grab bit 2 */
        (*msg)->sms.mclass = dcs & 0x03; /* grab bits 1,0 */
        (*msg)->sms.alt_dcs = 1; /* set 0xFX data coding */
    }
    
    /* Non-MWI Mode 0 */
    else if ((dcs & 0xC0) == 0x00) { 
        (*msg)->sms.alt_dcs = 0;
        (*msg)->sms.compress = ((dcs & 0x20) == 0x20) ? 1 : 0; /* grab bit 5 */
        (*msg)->sms.mclass = ((dcs & 0x10) == 0x10) ? dcs & 0x03 : MC_UNDEF; 
						/* grab bit 0,1 if bit 4 is on */
        (*msg)->sms.coding = (dcs & 0x0C) >> 2; /* grab bit 3,2 */
    }

    /* MWI */
    else if ((dcs & 0xC0) == 0xC0) { 
        (*msg)->sms.alt_dcs = 0;
        (*msg)->sms.coding = ((dcs & 0x30) == 0x30) ? DC_UCS2 : DC_7BIT;
        if (!(dcs & 0x08))
            dcs |= 0x04; /* if bit 3 is active, have mwi += 4 */
        dcs &= 0x07;
        (*msg)->sms.mwi = dcs ; /* grab bits 1,0 */
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
int sms_msgdata_len(Msg* msg) 
{
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


int sms_swap(Msg *msg) 
{
    Octstr *sender = NULL;

    if (msg->sms.sender != NULL && msg->sms.receiver != NULL) {
        sender = msg->sms.sender;
        msg->sms.sender = msg->sms.receiver;
        msg->sms.receiver = sender;

        return 1;
    }

    return 0;
}

