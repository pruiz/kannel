/*
 * ota_prov.h: OTA settings and bookmarks provisioning routines
 *
 * This module contains routines for the SMS OTA (auto configuration) message 
 * creation and manipulation for the sendota HTTP interface.
 *
 * Official Nokia and Ericsson WAP OTA configuration settings coded 
 * by Stipe Tolj <tolj@wapme-systems.de>, Wapme Systems AG.
 * 
 * XML compiler by Aarno Syvänen <aarno@wiral.com>, Wiral Ltd.
 */

#ifndef OTA_PROV_H
#define OTA_PROV_H

#include "gwlib/gwlib.h"


/*
 * Our WSP data: a compiled OTA document
 * Return -2 when header error, -1 when compile error, 0 when no error
 */
int ota_pack_message(Msg **msg, Octstr *ota_doc, Octstr *doc_type, 
                     Octstr *from, Octstr *phone_number);

/*
 * Tokenizes a given 'ota-setting' group (without using the xml compiler) to
 * a binary message and returns the whole message including sender and 
 * receiver numbers.
 */
Msg *ota_tokenize_settings(CfgGroup *grp, Octstr *from, Octstr *receiver);

/*
 * Tokenizes a given 'ota-bookmark' group (without using the xml compiler) to
 * a binary message and returns the whole message including sender and 
 * receiver numbers.
 */
Msg *ota_tokenize_bookmarks(CfgGroup *grp, Octstr *from, Octstr *receiver);


#endif /* OTA_PROV_H */
