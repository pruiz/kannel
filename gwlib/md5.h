/*
 * md5.h - MD5 digest algorithm
 */

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
   rights reserved.

   License to copy and use this software is granted provided that it
   is identified as the "RSA Data Security, Inc. MD5 Message-Digest
   Algorithm" in all material mentioning or referencing this software
   or this function.

   License is also granted to make and use derivative works provided
   that such works are identified as "derived from the RSA Data
   Security, Inc. MD5 Message-Digest Algorithm" in all material
   mentioning or referencing the derived work.

   RSA Data Security, Inc. makes no representations concerning either
   the merchantability of this software or the suitability of this
   software for any particular purpose. It is provided "as is"
   without express or implied warranty of any kind.

   These notices must be retained in any copies of any part of this
   documentation and/or software.
 */

#ifndef MD5_H
#define MD5_H

/* MD5 context. */
typedef struct {
    unsigned int state[4];        /* state (ABCD) */
    unsigned int count[2];        /* number of bits, modulo 2^64 (lsb first) */
    unsigned char buffer[64];   /* input buffer */
} md5_ctx;

/*
 * Calculates the MD5 digest key of a given Octstr.
 * Returns NULL in case NULL has been given as argument.
 */
Octstr *md5(Octstr *data);

#endif

