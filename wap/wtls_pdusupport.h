#ifndef PDUSUPPORT_H
#define PDUSUPPORT_H

int pack_int16(Octstr *data, long charpos, int i);
int pack_int32(Octstr *data, long charpos, long i);
int pack_octstr(Octstr *data, long charpos, Octstr *opaque);
int pack_octstr16(Octstr *data, long charpos, Octstr *opaque);
int pack_octstr_fixed(Octstr *data, long charpos, Octstr *opaque);
int pack_random(Octstr *data, long charpos, Random *random);
int pack_dhparams(Octstr *data, long charpos, DHParameters *dhparams);
int pack_ecparams(Octstr *data, long charpos, ECParameters *ecparams);
int pack_param_spec(Octstr *data, long charpos, ParameterSpecifier *pspec);
int pack_public_key(Octstr *data, long charpos, PublicKey *key, PublicKeyType key_type);
int pack_rsa_pubkey(Octstr *data, long charpos, RSAPublicKey *key);
int pack_dh_pubkey(Octstr *data, long charpos, DHPublicKey *key);
int pack_ec_pubkey(Octstr *data, long charpos, ECPublicKey *key);
int pack_rsa_secret(Octstr *data, long charpos, RSASecret *secret);
int pack_rsa_encrypted_secret(Octstr *data, long charpos, RSAEncryptedSecret *secret);
int pack_key_exchange_id(Octstr *data, long charpos, KeyExchangeId *keyexid);
int pack_array(Octstr *data, long charpos, List *array);
int pack_key_list(Octstr *data, long charpos, List *key_list);
int pack_ciphersuite_list(Octstr *data, long charpos, List *ciphersuites);
int pack_compression_method_list(Octstr *data, long charpos, List *compmethod_list);
int pack_identifier(Octstr *data, long charpos, Identifier *ident);
int pack_signature(Octstr *data, long charpos, Signature *sig);
int pack_wtls_certificate(Octstr *data, long charpos, WTLSCertificate *cert);


int unpack_int16(Octstr *data, long *charpos);
long unpack_int32(Octstr *data, long *charpos);
Octstr * unpack_octstr(Octstr *data, long *charpos);
Octstr * unpack_octstr16(Octstr *data, long *charpos);
Octstr * unpack_octstr_fixed(Octstr *data, long *charpos, long length);
Random * unpack_random(Octstr *data, long *charpos);
DHParameters * unpack_dhparams(Octstr *data, long *charpos);
ECParameters * unpack_ecparams(Octstr *data, long *charpos);
ParameterSpecifier * unpack_param_spec(Octstr *data, long *charpos);
PublicKey * unpack_public_key(Octstr *data, long *charpos, PublicKeyType key_type);
RSAPublicKey * unpack_rsa_pubkey(Octstr *data, long *charpos);
DHPublicKey * unpack_dh_pubkey(Octstr *data, long *charpos);
ECPublicKey * unpack_ec_pubkey(Octstr *data, long *charpos);
RSASecret * unpack_rsa_secret(Octstr *data, long *charpos);
RSAEncryptedSecret * unpack_rsa_encrypted_secret(Octstr *data, long *charpos);
KeyExchangeId * unpack_key_exchange_id(Octstr *data, long *charpos);
List * unpack_array(Octstr *data, long *charpos);
List * unpack_ciphersuite_list(Octstr *data, long *charpos);
List * unpack_key_list(Octstr *data, long *charpos);
List * unpack_compression_method_list(Octstr *data, long *charpos);
Identifier * unpack_identifier(Octstr *data, long *charpos);
Signature * unpack_signature(Octstr *data, long *charpos);
WTLSCertificate * unpack_wtls_certificate(Octstr *data, long *charpos);

void dump_int16(unsigned char *dbg, int level, int i);
void dump_int32(unsigned char *dbg, int level, long i);
void dump_octstr(unsigned char *dbg, int level, Octstr *opaque);
void dump_octstr16(unsigned char *dbg, int level, Octstr *opaque);
void dump_octstr_fixed(unsigned char *dbg, int level, Octstr *opaque);
void dump_random(unsigned char *dbg, int level, Random *random);
void dump_dhparams(unsigned char *dbg, int level, DHParameters *dhparams);
void dump_ecparams(unsigned char *dbg, int level, ECParameters *ecparams);
void dump_param_spec(unsigned char *dbg, int level, ParameterSpecifier *pspec);
void dump_public_key(unsigned char *dbg, int level, PublicKey *key, PublicKeyType key_type);
void dump_rsa_pubkey(unsigned char *dbg, int level, RSAPublicKey *key);
void dump_dh_pubkey(unsigned char *dbg, int level, DHPublicKey *key);
void dump_ec_pubkey(unsigned char *dbg, int level, ECPublicKey *key);
void dump_rsa_secret(unsigned char *dbg, int level, RSASecret *secret);
void dump_rsa_encrypted_secret(unsigned char *dbg, int level, RSAEncryptedSecret *secret);
void dump_key_exchange_id(unsigned char *dbg, int level, KeyExchangeId *keyexid);
void dump_array(unsigned char *dbg, int level, List *array);
void dump_key_list(unsigned char *dbg, int level, List *key_list);
void dump_ciphersuite_list(unsigned char *dbg, int level, List *ciphersuites);
void dump_compression_method_list(unsigned char *dbg, int level, List *compmethod_list);
void dump_identifier(unsigned char *dbg, int level, Identifier *ident);
void dump_signature(unsigned char *dbg, int level, Signature *sig);
void dump_wtls_certificate(unsigned char *dbg, int level, WTLSCertificate *cert);

void destroy_rsa_pubkey(RSAPublicKey *key);
void destroy_array(List *array);
void destroy_identifier(Identifier *ident);


#endif
