#ifndef PKI_H
#define PKI_H
#include "gwlib.h"
#include <openssl/x509.h>

/* pki.c /pki.h contain an interface to openssl to read and manipulate various
   encryption and certificate functions */

void pki_init(void);
void pki_shutdown(void);
void get_cert_from_file(Octstr *s, X509 **x509);
void get_privkey_from_file(Octstr *s, RSA **priv_key, Octstr *password);

void dump_cert(X509 *x509);
void dump_privkey(RSA *priv_key);


#endif PKI_H
