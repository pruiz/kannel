/*
 * wap-appl.h - interface to application layer
 */

#ifndef WAP_APPL_H
#define WAP_APPL_H

#include "wap/wap.h"

void wap_appl_init(void);
void wap_appl_dispatch(WAPEvent *event);
void wap_appl_shutdown(void);
long wap_appl_get_load(void);

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

#endif
