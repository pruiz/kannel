/*
 * sms.h - definitions specific to SMS but not particular to any SMSC protocol.
 *
 * This file current contains very little, but sms features that are
 * currently implemented separately in each protocol should be extracted
 * and placed here.
 */

#ifndef SMS_H
#define SMS_H

/*
 * Data coding scheme values to use when encoding messages, for
 * protocols that use GSM 03.38 data coding scheme values.
 * Used by the at, cimd2, ois, and smpp drivers.
 */
enum dcs_body_type {
    DCS_GSM_TEXT = 0,      
    DCS_OCTET_DATA = 4    /* flag_8bit */
};

#endif
