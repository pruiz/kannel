/*
 * gw/wap-maps.h - URL mapping 
 * 
 * Bruno Rodrigues  <bruno.rodrigues@litux.org>
 */

/*
 * Adds a mapping entry to map an url into an (all optional)
 * - an description name
 * - new url
 * - a query-string parameter name to send msisdn, if available
 * - an header name to send msisdn, if available
 * - msisdn header format
 * - accept cookies (1), not accept (0) or use global settings (-1)
 */
void wap_map_add_url(Octstr *name, Octstr *url, Octstr *map_url,
                     Octstr *send_msisdn_query,
                     Octstr *send_msisdn_header,
                     Octstr *send_msisdn_format,
                     int accept_cookies);

/*
 * Adds a mapping entry to map user/pass into a description
 * name and a msisdn
 */
void wap_map_add_user(Octstr *name, Octstr *user, Octstr *pass,
                      Octstr *msisdn);

/*
 * Destruction routines
 */
void wap_map_destroy(void);
void wap_map_user_destroy(void);

/* 
 * Maybe rewrite URL, if there is a mapping. This is where the runtime
 * lookup comes in (called from further down this file, wsp_http.c)
 */
void wap_map_url(Octstr **osp, Octstr **send_msisdn_query, 
                             Octstr **send_msisdn_header, 
                             Octstr **send_msisdn_format, int *accept_cookies);

/* 
 * Provides a mapping facility for resolving user and pass to an
 * predefined MSISDN.
 * Returns 1, if mapping has been found, 0 otherwise.
 */
int wap_map_user(Octstr **msisdn, Octstr *user, Octstr *pass);

/* 
 * Called during configuration read, this adds a mapping for the source URL
 * "DEVICE:home", to the given destination. The mapping is configured
 * as an in/out prefix mapping.
 */
void wap_map_url_config_device_home(char *to);

/* 
 * Called during configuration read, once for each "map-url" statement.
 * Interprets parameter value as a space-separated two-tuple of src and dst.
 */
void wap_map_url_config(char *s);


