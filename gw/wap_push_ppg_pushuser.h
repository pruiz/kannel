/*
 * wap_push_ppg_pushuser.h: Header of the push user module. This means
 * currently authentication and smsc routing. 
 *
 * Only WAP-165-PushArchOverview-19991108-a, an informal document, mentions
 * pi authentication. (See chapter 13.) So this is definitely left for 
 * implementors.
 * Basic authentication is defined in rfc 2617. Note that https connections
 * are handled by our http module.
 *
 * By Aarno Syvänen for Wiral Ltd and Global Networks Inc
 */

#ifndef WAP_PUSH_PPG_PUSHUSER_H
#define WAP_PUSH_PPG_PUSHUSER_H

#include "gwlib/gwlib.h"

/*
 * This function initializes the module and push users data stucture, contain-
 * ing user specific  data for all push user accounts. This function MUST be
 * called before any other functions of this module.
 */
int wap_push_ppg_pushuser_list_add(List *l, long number_of_pushes, 
                                   long number_of_users);

/*
 * This function does clean up for module shutdown. This module MUST be called
 * when the caller of this module is shut down.
 */
void wap_push_ppg_pushuser_list_destroy(void);

/*
 * This function does authentication possible before compiling the control 
 * document. This means:
 *           a) password authentication by url or by headers (it is, by basic
 *              authentication response, see rfc 2617, chapter 2) 
 *           b) if this does not work, basic authentication by challenge - 
 *              response 
 *           c) enforcing various ip lists
 *
 * Check does ppg allows a connection from this at all, then try to find username 
 * and password from headers, then from url. If both fails, try basic authentica-
 * tion. Then check does this user allow a push from this ip, then check the pass-
 * word.
 *
 * For protection against brute force and partial protection for denial of serv-
 * ice attacks, an exponential backup algorithm is used. Time when a specific ip  
 * is allowed to reconnect, is stored in Dict next_try. If an ip tries to recon-
 * nect before this (three attemps are allowed, then exponential seconds are add-
 * ed to the limit) we make a new challenge. We do the corresponding check before
 * testing passwords; after all, it is an authorization failure that causes a new
 * challenge. 
 *
 * Rfc 2617, chapter 1 states that if we do not accept credentials of an user's, 
 * we must send a new challenge to the user.
 *
 * Output an authenticated username.
 * This function should be called only when there are a push users list; the 
 * caller is responsible for this.
 */
int wap_push_ppg_pushuser_authenticate(HTTPClient *client, List *cgivars, 
                                       Octstr *ip, List *headers, 
                                       Octstr **username);

/*
 * This function checks phone number for allowed prefixes, black lists and 
 * white lists. Note that the phone number necessarily follows the interna-
 * tional format (this is checked by our pap compiler).
 */
int wap_push_ppg_pushuser_client_phone_number_acceptable(Octstr *username, 
    Octstr *number);

int wap_push_ppg_pushuser_search_ip_from_wildcarded_list(Octstr *haystack, 
    Octstr *needle, Octstr *list_sep, Octstr *ip_sep);

/*
 * Returns smsc pushes by this user must use, or NULL when error.
 */
Octstr *wap_push_ppg_pushuser_smsc_id_get(Octstr *username);

/*
 * Returns default dlr url for this user, or NULL when error.
 */
Octstr *wap_push_ppg_pushuser_dlr_url_get(Octstr *username);

/*
 * Returns default dlr smsbox id for this user, or NULL when error.
 */
Octstr *wap_push_ppg_pushuser_smsbox_id_get(Octstr *username);
#endif
