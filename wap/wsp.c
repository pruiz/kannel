/*
 * wsp.c - Parts of WSP shared between session oriented and connectionless mode
 */


#include <string.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"
#include "wsp_strings.h"

/***********************************************************************
 * Public functions
 */


/* Convert HTTP status codes to WSP status codes according
 * to WSP Table 36, Status Code Assignments. */
long wsp_convert_http_status_to_wsp_status(long http_status) {
	long hundreds, singles;

	/*
	 * The table is regular, and can be expected to stay regular in
	 * future versions of WSP.  The status value is read as XYY,
	 * so that X is the first digit and Y is the value of the
	 * second two digits.  This is encoded as a hex value 0xAB,
	 * where A == X and B == YY.
	 * This limits YY to the range 0-15, so an exception is made
	 * to allow larger YY values when X is 4.  X value 5 is moved up
	 * to A value 6 to allow more room for YY when X is 4.
	 */

	hundreds = http_status / 100;
	singles = http_status % 100;

	if ((hundreds == 4 && singles > 31) ||
	    (hundreds != 4 && singles > 15) ||
	    hundreds < 1 || hundreds > 5)
		goto bad_status;

	if (hundreds > 4)
		hundreds++;

	return hundreds * 16 + singles;

bad_status:
	error(0, "WSP: Unknown status code used internally. Oops.");
	return 0x60; /* Status 500, or "Internal Server Error" */
}
