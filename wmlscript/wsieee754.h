/*
 *
 * wsieee754.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Functions to manipulate ANSI/IEEE Std 754-1985 binary floating-point
 * numbers.
 *
 */

#ifndef WSIEEE754_H
#define WSIEEE754_H

/********************* Types and definitions ****************************/

/* Return codes for encoding and decoding functions. */
typedef enum
{
  /* The operation was successful. */
  WS_IEEE754_OK,

  /* The value is `Not a Number' NaN. */
  WS_IEEE754_NAN,

  /* The valueis positive infinity. */
  WS_IEEE754_POSITIVE_INF,

  /* The value is negative infinity. */
  WS_IEEE754_NEGATIVE_INF
} WsIeee754Result;

/********************* Special values ***********************************/

/* `Not a Number' NaN */
extern unsigned char ws_ieee754_nan[4];

/* Positive infinity. */
extern unsigned char ws_ieee754_positive_inf[4];

/* Positive infinity. */
extern unsigned char ws_ieee754_negative_inf[4];

/********************* Global functions *********************************/

/* Encode the floating point number `value' to the IEEE-754 single
   precision format.  The function stores the encoded value to the
   buffer `buf'.  The buffer `buf' must have 32 bits (4 bytes) of
   space.  The function returns WsIeee754Result return value.  It
   describes the format of the encoded value in `buf'.  In all cases,
   the function generates the corresponding encoded value to the
   buffer `buf'. */
WsIeee754Result ws_ieee754_encode_single(double value, unsigned char *buf);

/* Decode the IEEE-754 encoded number `buf' into a floating point
   number.  The argument `buf' must have 32 bits of data.  The
   function returns a result code which describes the success of the
   decode operation.  If the result is WS_IEEE754_OK, the resulting
   floating point number is returned in `value_return'. */
WsIeee754Result ws_ieee754_decode_single(unsigned char *buf,
					 double *value_return);

/* Get the sign bit from the IEEE-754 single format encoded number
   `buf'.  The buffer `buf' must have 32 bits of data. */
WsUInt32 ws_ieee754_single_get_sign(unsigned char *buf);

/* Get the exponent from the IEEE-754 single format encoded number
   `buf'.  The buffer `buf' must have 32 bits of data.  The returned
   value is the biased exponent. */
WsUInt32 ws_ieee754_single_get_exp(unsigned char *buf);

/* Get the mantissa from the IEEE-754 single format encoded number
   `buf'.  The buffer `buf' must have 32 bits of data. */
WsUInt32 ws_ieee754_single_get_mant(unsigned char *buf);

#endif /* not WSIEEE754_H */
