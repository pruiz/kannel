/*
 * wap-appl.h - interface to application layer
 */

#ifndef WAP_APPL_H
#define WAP_APPL_H

#include "wap/wap.h"

void wap_appl_init(Cfg *cfg);
void wap_appl_dispatch(WAPEvent *event);
void wap_appl_shutdown(void);
long wap_appl_get_load(void);

/* configure an URL mapping (new version) */
void wsp_http_url_map(Octstr *name, Octstr *url, Octstr *map_url, 
                      Octstr *send_msisdn_query, 
                      Octstr *send_msisdn_header,
                      Octstr *send_msisdn_format, 
                      int accept_cookies);

/* configure an URL mapping; parses string on whitespace, uses left
 * part for the source URL, and right part for the destination URL
 */
void wsp_http_map_url_config(char *);

/* configure an URL mapping from source DEVICE:home to given string */
void wsp_http_map_url_config_device_home(char *);

/* show all configured URL mappings */
void wsp_http_map_url_config_info(void);

/* Free URL mapping table. */
void wsp_http_map_destroy(void);

/* configure an User mapping */
void wsp_http_user_map(Octstr *name, Octstr *user, Octstr *pass, 
                      Octstr *msisdn);

/* show all configured User mappings */
void wsp_http_map_user_config_info(void);

/* Free User mapping table. */
void wsp_http_map_user_destroy(void);

#endif
