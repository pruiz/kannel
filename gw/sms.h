/*
 * sms.h - definitions specific to SMS but not particular to any SMSC protocol.
 *
 * Sms features that are currently implemented separately in each protocol 
 * should be extracted and placed here.
 */

/*
 * DCS Encoding, acording to ETSI 03.38 v7.2.0
 *
 * 00abcdef
 *      bit 5 (a) indicates compressed text
 *      bit 4 (b) indicates Message Class value presence
 *      bits 3,2 (c,d) indicates Data Coding (00=7bit, 01=8bit, 10=UCS2)
 *      bits 1,0 (e,f) indicates Message Class, if bit 4(b) is active
 *
 * 11110abc
 *      bit 2 (a) indicates 0=7bit, 1=8bit
 *      bits 1,0 (b,c) indicates Message Class
 *
 * 11abc0de
 *      bits 5,4 (a,b) indicates 00=discard message, 01=store message
 *                               10=store message and text is UCS2
 *      bit 3 (c) indicates indication active
 *      bits 1,0 (d,e) indicates indicator (00=voice mail, 01=fax,
 *                                          10=email, 11=other)
 */


#ifndef SMS_H
#define SMS_H

#include "msg.h"

#define SMS_PARAM_UNDEFINED  -1

#define MC_UNDEF   SMS_PARAM_UNDEFINED
#define MC_CLASS0  0
#define MC_CLASS1  1
#define MC_CLASS2  2
#define MC_CLASS3  3

#define MWI_UNDEF      SMS_PARAM_UNDEFINED
#define MWI_VOICE_ON   0
#define MWI_FAX_ON     1
#define MWI_EMAIL_ON   2
#define MWI_OTHER_ON   3
#define MWI_VOICE_OFF  4
#define MWI_FAX_OFF    5
#define MWI_EMAIL_OFF  6
#define MWI_OTHER_OFF  7

#define DC_UNDEF  SMS_PARAM_UNDEFINED
#define DC_7BIT   0
#define DC_8BIT   1
#define DC_UCS2   2

#define COMPRESS_UNDEF  SMS_PARAM_UNDEFINED
#define COMPRESS_OFF    0
#define COMPRESS_ON     1

#define RPI_UNDEF  SMS_PARAM_UNDEFINED
#define RPI_OFF    0
#define RPI_ON     1

#define SMS_7BIT_MAX_LEN 160
#define SMS_8BIT_MAX_LEN 140
#define SMS_UCS2_MAX_LEN 70

/* Encode DCS using sms fields
 * mode = 0= encode using 00xxxxxx, 1= encode using 1111xxxx mode
 */
int fields_to_dcs(Msg *msg, int mode);


/*
 * Decode DCS to sms fields
 *  returns 0 if dcs is invalid
 */
int dcs_to_fields(Msg **msg, int mode);


/*
 * Compute length of the message data in Msg after it will be converted 
 * to the proper coding. 
 * If coding is 7 bit, then sms_msgdata_len will return the number of 
 * septets this message will convert to, taking into account GSM 03.38
 * escape sequences of special chars, which would count as two septets.
 */
int sms_msgdata_len(Msg *msg);


/*
 * Swap an MO message to an MT message (hence swap receiver/sender addresses)
 * and vice versa for internal bearerbox rerouting (if needed).
 * Returns 1 if successfull, 0 otherwise.
 */
int sms_swap(Msg *msg);


#endif
