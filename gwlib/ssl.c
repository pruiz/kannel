/*  
 * ssl.c - implements SSL specific routines and types 
 * 
 * This file implements the secure socket layer (SSL) specific  
 * routines and types. 
 * 
 * This product includes software developed by Ralf S. Engelschall  
 * <rse@engelschall.com> for use in the mod_ssl project  
 * (http://www.modssl.org/). 
 * 
 * Stipe Tolj <tolj@wapme-systems.de>  
 * for Kannel Project and Wapme Systems AG 
 */ 
 
#include "gwlib/gwlib.h" 
 
#ifdef HAVE_LIBSSL 
  
#include <openssl/ssl.h> 
 
int SSL_smart_shutdown(SSL *ssl) 
{ 
    int i; 
    int rc; 
 
    /* 
     * Repeat the calls, because SSL_shutdown internally dispatches through a 
     * little state machine. Usually only one or two interation should be 
     * needed, so we restrict the total number of restrictions in order to 
     * avoid process hangs in case the client played bad with the socket 
     * connection and OpenSSL cannot recognize it. 
     */ 
    rc = 0; 
    for (i = 0; i < 4 /* max 2x pending + 2x data = 4 */; i++) { 
        if ((rc = SSL_shutdown(ssl))) 
            break; 
    } 
    return rc; 
} 
 
#endif /* HAVE_LIBSSL */
