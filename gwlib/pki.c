/*
 * pki.c: PKI and certificate handling routines
 *
 */

#include <stdio.h>

#include "gwlib/gwlib.h"
 
#if (HAVE_WTLS_OPENSSL)
 
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include "pki.h"

void pki_init(void)
{
        OpenSSL_add_all_algorithms();
        ERR_load_crypto_strings();
}

void pki_shutdown(void)
{
        EVP_cleanup();
}


void get_cert_from_file(Octstr *s, X509 **x509)
{
		char *filename;
		
        /* Check errors!!!! */
        FILE* fp;
        Octstr* foo;
        
        /* Open the file specified by "s" */
		filename = octstr_get_cstr(s);
        fp = fopen(filename,"r");
        if (fp == NULL) warning(0,"Can't read certificate %s", filename);

        /* Load up that there certificate */
        *x509 = PEM_read_X509(fp,NULL,NULL,NULL);

        /* Close the file specified by "s" */        
        fclose(fp);

        if (x509 == NULL) {
                ERR_print_errors_fp (stderr);
        }
}

void get_privkey_from_file(Octstr* s, RSA** priv_key, Octstr* passwd)
{
		char *password;
		char *filename;
		
        /* Check errors!!!! */
        FILE* fp;
        Octstr* foo;

		filename = octstr_get_cstr(s);
		password = octstr_get_cstr(passwd);
        
		/* Open the file specified by "s" */
        fp = fopen(filename,"r");
        if (fp == NULL) warning(0,"Can't read private key %s", filename);
        
        /* Load up that there certificate */
        *priv_key = PEM_read_RSAPrivateKey(fp,NULL,NULL,password);

        /* Close the file specified by "s" */        
        fclose(fp);
        
        if (priv_key == NULL) { 
                ERR_print_errors_fp (stderr);
        }
}

void dump_cert(X509* x509)
{
        
}


void dump_privkey(RSA* priv_key)
{
}

#endif
