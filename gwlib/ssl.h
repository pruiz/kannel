/*  
 * ssl.h - declares SSL specific routines and types 
 * 
 * This file defines the secure socket layer (SSL) specific  
 * routines and types. 
 * 
 * Stipe Tolj <tolj@wapme-systems.de>  
 * for Kannel Project and Wapme Systems AG 
 */ 
 
#ifdef HAVE_LIBSSL 
 
int SSL_smart_shutdown(SSL *ssl); 
 
#endif /* HAVE_LIBSSL */
