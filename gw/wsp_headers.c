/*
 * wsp_headers.c - Implement WSP PDU headers
 * 
 * References:
 *   WSP specification version 1.1
 *   RFC 2068, Hypertext Transfer Protocol HTTP/1.1
 *
 * Kalle Marjola <rpr@wapit.com>
 * Modified by Richard Braakman <dark@wapit.com>
 */

#include <string.h>


#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_headers.h"
#include "wsp-strings.h"


#define WSP_FIELD_VALUE_NUL_STRING	1
#define WSP_FIELD_VALUE_ENCODED 	2
#define WSP_FIELD_VALUE_DATA		3
#define WSP_FIELD_VALUE_NONE		4 /* secondary_field_value only */

/* The value defined as Quote in 8.4.2.1 */
#define WSP_QUOTE  127

/*
 * get field value and return its type as predefined data types
 * There are three kinds of field encodings:
 *   WSP_FIELD_VALUE_NUL_STRING: 0-terminated string
 *   WSP_FIELD_VALUE_ENCODED: short integer, range 0-127
 *   WSP_FIELD_VALUE_DATA: octet string defined by length
 * The function will return one of those values, and modify the parse context
 * to make it easy to get the field data.
 *   WSP_FIELD_VALUE_NUL_STRING: Leave parsing position at start of string
 *   WSP_FIELD_VALUE_ENCODED: Put value in *well_known_value, leave
 *        parsing position after field value.
 *   WSP_FIELD_VALUE_DATA: Leave parsing position at start of data, and set
 *        a parse limit at the end of data.
 */
static int field_value(Context *context, int *well_known_value) {
	int val;
	unsigned long len;

	val = parse_get_char(context);
	if (val < 31) {
		*well_known_value = -1;
		parse_limit(context, val);
		return WSP_FIELD_VALUE_DATA;
    	} else if (val == 31) {
		*well_known_value = -1;
		len = parse_get_uintvar(context);
		parse_limit(context, len);
		return WSP_FIELD_VALUE_DATA;
    	} else if (val > 127) {
		*well_known_value = val - 128;
		return WSP_FIELD_VALUE_ENCODED;
	} else if (val == WSP_QUOTE) {  /* 127 */
		*well_known_value = -1;
		/* We already consumed the Quote */
		return WSP_FIELD_VALUE_NUL_STRING;
    	} else {
		*well_known_value = -1;
		/* Un-parse the character we just read */
		parse_skip(context, -1);
		return WSP_FIELD_VALUE_NUL_STRING;
   	 }
}

/* Multi-octet-integer is defined in 8.4.2.1 */
static long unpack_multi_octet_integer(Context *context, long len) {
	long val = 0;

	if (len >= (long) sizeof(val) || len < 0)
		return -1;

	while (len > 0) {
		val = val * 256 + parse_get_char(context);
		len--;
	}

	if (parse_error(context))
		return -1;

	return val;
}

/* This function is similar to field_value, but it is used at various
 * places in the grammar where we expect either an Integer-value
 * or some kind of NUL-terminated text.
 * 
 * Return values are just like field_value except that WSP_FIELD_VALUE_DATA
 * will not be returned.
 *
 * As a special case, we parse a 0-length Long-integer as an
 * WSP_FIELD_VALUE_NONE, so that we can distinguish between No-value
 * and an Integer-value of 0.  (A real integer 0 would be encoded as
 * a Short-integer; the definition of Long-integer seems to allow
 * 0-length integers, but the definition of Multi-octet-integer does
 * not, so this is an unclear area of the specification.)
 */
static int secondary_field_value(Context *context, long *result) {
	int val;
	long length;

	val = parse_get_char(context);
	if (val == 0) {
		*result = 0;
		return WSP_FIELD_VALUE_NONE;
	} else if (val < 31) {
		*result = unpack_multi_octet_integer(context, val);
		return WSP_FIELD_VALUE_ENCODED;
	} else if (val == 31) {
		length = parse_get_uintvar(context);
		*result = unpack_multi_octet_integer(context, length);
		return WSP_FIELD_VALUE_ENCODED;
	} else if (val > 127) {
		*result = val - 128;
		return WSP_FIELD_VALUE_ENCODED;
	} else if (val == WSP_QUOTE) {  /* 127 */
		*result = -1;
		return WSP_FIELD_VALUE_NUL_STRING;
	} else {
		*result = -1;
		/* Un-parse the character we just read */
		parse_skip(context, -1);
		return WSP_FIELD_VALUE_NUL_STRING;
	}
}

/* Integer-value is defined in 8.4.2.3 */
static Octstr *unpack_integer_value(Context *context) {
	Octstr *decoded;
	unsigned long value;
	int val;

	val = parse_get_char(context);
	if (val < 31) {
		value = unpack_multi_octet_integer(context, val);
	} else if (val > 127) {
		value = val - 128;
	} else {
		warning(0, "WSP headers: bad integer-value.");
		return NULL;
	}

	decoded = octstr_create("");
	octstr_append_decimal(decoded, value);
	return decoded;
}

/* Q-value is defined in 8.4.2.3 */
static Octstr *convert_q_value(int q) {
	Octstr *result = NULL;

	/* When quality factor 0 and quality factors with one or two
	 * decimal digits are encoded, they shall be multiplied by 100
	 * and incremented by one, so that they encode as a one-octet
	 * value in range 1-100. */
	if (q >= 1 && q <= 100) {
		q = q - 1;
		result = octstr_create("0.");
		octstr_append_char(result, (q / 10) + '0');
		if (q % 10 > 0)
			octstr_append_char(result, (q % 10) + '0');
		return result;
	}

	/* Three decimal quality factors shall be multiplied with 1000
	 * and incremented by 100. */
	if (q > 100 && q <= 1000) {
		q = q - 100;
		result = octstr_create("0.");
		octstr_append_char(result, (q / 100) + '0');
		if (q % 100 > 0)
			octstr_append_char(result, (q / 10 % 10) + '0');
		if (q % 10 > 0)
			octstr_append_char(result, (q % 10) + '0');
		return result;
	}

	return NULL;
}

/* Q-value is defined in 8.4.2.3 */
static Octstr *unpack_q_value(Context *context) {
	int c, c2;

	c = parse_get_char(context);
	if (c < 0)
		return NULL;

	if (c & 0x80) {
		c2 = parse_get_char(context);
		if (c2 < 0 || (c2 & 0x80))
			return NULL;
		c = ((c & 0x7f) << 8) + c2;
	}

	return convert_q_value(c);
}

/* Version-value is defined in 8.4.2.3 */
static Octstr *unpack_version_value(long value) {
	Octstr *result;
	int major, minor;

	major = ((value >> 4) & 0x7);
	minor = (value & 0xf);

	result = octstr_create("");
	octstr_append_char(result, major + '0');
	if (minor != 15) {
		octstr_append_char(result, '.');
		octstr_append_decimal(result, minor);
	}

	return result;
}

/* Called with the parse limit set to the end of the parameter data,
 * and decoded containing the unpacked header line so far.
 * Parameter is defined in 8.4.2.4. */
static int unpack_parameter(Context *context, Octstr *decoded) {
	Octstr *parm = NULL;
	Octstr *value = NULL;
	int ret;
	long type;
	long val;
	
	ret = secondary_field_value(context, &type);
	if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
		warning(0, "bad parameter");
		goto error;
	}

	if (ret == WSP_FIELD_VALUE_ENCODED) {
		/* Typed-parameter */
		parm = wsp_parameter_to_string(type);
		if (!parm)
			warning(0, "Unknown parameter %02lx.", type);
	} else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
		/* Untyped-parameter */
		parm = parse_get_nul_string(context);
		if (!parm)
			warning(0, "Format error in parameter.");
		type = -1;
		/* We treat Untyped-value as a special type.  Its format
		 * Integer-value | Text-value is pretty similar to most
		 * typed formats. */
	} else {
		panic(0, "Unknown secondary field value type %d.", ret);
	}

	if (type == 0x00) /* q */
		value = unpack_q_value(context);
	else {
		ret = secondary_field_value(context, &val);
		if (parse_error(context)) {
			warning(0, "bad parameter value");
			goto error;
		}

		if (ret == WSP_FIELD_VALUE_ENCODED) {
			switch (type) {
			case -1: /* untyped: Integer-value */
			case 3: /* type: Integer-value */
			case 8: /* padding: Short-integer */
				value = octstr_create("");
				octstr_append_decimal(value, val);
				break;
			case 0: /* q, already handled above */
				gw_assert(0);
				break;
			case 1: /* charset: Well-known-charset */
				value = wsp_charset_to_string(val);
				if (!value)
					warning(0, "Unknown charset %04lx.", val);
				break;
			case 2: /* level: Version-value */
				value = unpack_version_value(val);
				break;
			case 5: /* name: Text-string */
			case 6: /* filename: Text-string */
				warning(0, "Text-string parameter with integer encoding");
				break;
			case 7: /* differences: Field-name */
				value = wsp_header_to_string(val);
				if (!value)
					warning(0, "Unknown differences header %02lx.", val);
				break;
			default:
				warning(0, "Unknown parameter encoding %02lx.",
					type);
				break;
			}
		} else if (ret == WSP_FIELD_VALUE_NONE) {
			value = octstr_create("");
		} else {
			gw_assert(ret == WSP_FIELD_VALUE_NUL_STRING);
			/* Text-value = No-value | Token-text | Quoted-string */
			value = parse_get_nul_string(context);
			if (!value)
				warning(0, "Format error in parameter value.");
			else { 
				if (octstr_get_char(value, 0) == '"') {
					/* Quoted-string */
					octstr_append_char(value, '"');
				}
			}
		}
	}

	if (!parm || !value) {
		warning(0, "Skipping parameters");
		goto error;
	}

	octstr_append_cstr(decoded, "; ");
	octstr_append(decoded, parm);
	if (octstr_len(value) > 0) {
		octstr_append_char(decoded, '=');
		octstr_append(decoded, value);
	}
	octstr_destroy(parm);
	octstr_destroy(value);
	return 0;

error:
	parse_skip_to_limit(context);
	octstr_destroy(parm);
	octstr_destroy(value);
	parse_set_error(context);
	return -1;
}

static void unpack_all_parameters(Context *context, Octstr *decoded) {
	int ret = 0;

	while (ret >= 0 && !parse_error(context) &&
	       parse_octets_left(context) > 0) {
		ret = unpack_parameter(context, decoded);
	}
}

static void unpack_optional_q_value(Context *context, Octstr *decoded) {
	if (parse_octets_left(context) > 0) {
		Octstr *qval = unpack_q_value(context);
		if (qval) {
			octstr_append_cstr(decoded, "; q=");
			octstr_append(decoded, qval);
			octstr_destroy(qval);
		} else
			warning(0, "Bad q-value");
	}
}

/* Date-value is defined in 8.4.2.3 */
static Octstr *unpack_date_value(Context *context) {
	unsigned long timeval;
	int length;

	length = parse_get_char(context);
	if (length > 30) {
		warning(0, "WSP headers: bad date-value.");
		return NULL;
	}

	timeval = unpack_multi_octet_integer(context, length);
	return rfc2068_date_format(timeval);
}

/* Accept-general-form is defined in 8.4.2.7 */
static Octstr *unpack_accept_general_form(Context *context) {
	Octstr *decoded = NULL;
	int ret;
	long val;

	/* The definition for Accept-general-form looks quite complicated,
  	 * but the "Q-token Q-value" part fits the normal expansion of
	 * Parameter, so it simplifies to:
	 *  Value-length Media-range *(Parameter)
	 * and we've already parsed Value-length.
	 */

	/* We use this function to parse content-general-form too,
	 * because its definition of Media-type is identical to Media-range.
	 */

	ret = secondary_field_value(context, &val);
	if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
		warning(0, "bad media-range or media-type");
		return NULL;
	}

	if (ret == WSP_FIELD_VALUE_ENCODED) {
		decoded = wsp_content_type_to_string(val);
		if (!decoded) {
			warning(0, "Unknown content type 0x%02lx.", val);
			return NULL;
		}
	} else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
		decoded = parse_get_nul_string(context);
		if (!decoded) {
			warning(0, "Format error in content type");
			return NULL;
		}
	} else {
		panic(0, "Unknown secondary field value type %d.", ret);
	}

	unpack_all_parameters(context, decoded);
	return decoded;
}

/* Accept-charset-general-form is defined in 8.4.2.8 */
static Octstr *unpack_accept_charset_general_form(Context *context) {
	Octstr *decoded = NULL;
	int ret;
	long val;

	ret = secondary_field_value(context, &val);
	if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
		warning(0, "Bad accept-charset-general-form");
		return NULL;
	}

	if (ret == WSP_FIELD_VALUE_ENCODED) {
		decoded = wsp_charset_to_string(val);
		if (!decoded) {
			warning(0, "Unknown character set %04lx.", val);
			return NULL;
		}
	} else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
		decoded = parse_get_nul_string(context);
		if (!decoded) {
			warning(0, "Format error in accept-charset");
			return NULL;
		}
	} else {
		panic(0, "Unknown secondary field value type %d.", ret);
	}

	unpack_optional_q_value(context, decoded);
	return decoded;
}

/* Accept-language-general-form is defined in 8.4.2.10 */
static Octstr *unpack_accept_language_general_form(Context *context) {
	Octstr *decoded = NULL;
	int ret;
	long val;

	ret = secondary_field_value(context, &val);
	if (parse_error(context) || ret == WSP_FIELD_VALUE_NONE) {
		warning(0, "Bad accept-language-general-form");
		return NULL;
	}

	if (ret == WSP_FIELD_VALUE_ENCODED) {
		/* Any-language is handled by a special entry in the
		 * language table. */
		decoded = wsp_language_to_string(val);
		if (!decoded) {
			warning(0, "Unknown language %02lx.", val);
			return NULL;
		}
	} else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
		decoded = parse_get_nul_string(context);
		if (!decoded) {
			warning(0, "Format error in accept-language");
			return NULL;
		}
	} else {
		panic(0, "Unknown secondary field value type %d.", ret);
	}

	unpack_optional_q_value(context, decoded);
	return decoded;
}

/* Credentials is defined in 8.4.2.5 */
static Octstr *unpack_credentials(Context *context) {
	Octstr *decoded = NULL;
	int val;

	val = parse_peek_char(context);

	if (val == 128) {
		/* Basic authentication */
		Octstr *userid, *password;

		parse_skip(context, 1);

		userid = parse_get_nul_string(context);
		password = parse_get_nul_string(context);

		if (parse_error(context)) {
			octstr_destroy(userid);
			octstr_destroy(password);
		} else {
			/* Create the user-pass cookie */
			decoded = octstr_duplicate(userid);
			octstr_append_char(decoded, ':');
			octstr_append(decoded, password);

			/* XXX Deal with cookie that overflows the 76-per-line
			 * limit of base64.  Either go through and zap all
			 * CR LF sequences, or give the conversion function
			 * a flag or something to leave them out. */
			octstr_binary_to_base64(decoded);

			/* Zap the CR LF at the end */
			octstr_delete(decoded, octstr_len(decoded) - 2, 2);

			octstr_insert_data(decoded, 0, "Basic ", 6);

			octstr_destroy(userid);
			octstr_destroy(password);
		}
	} else if (val >= 32 && val < 128) {
		/* Generic authentication scheme */
		decoded = parse_get_nul_string(context);
		if (decoded)
			unpack_all_parameters(context, decoded);
	}

	if (!decoded)
		warning(0, "Cannot parse credentials.");

	return decoded;
}

/* Challenge is defined in 8.4.2.5 */
static Octstr *unpack_challenge(Context *context) {
	Octstr *decoded = NULL;
	Octstr *realm_value = NULL;
	int val;

	val = parse_get_char(context);
	if (val == 128) {
		/* Basic authentication */
		realm_value = parse_get_nul_string(context);
		if (realm_value) {
			decoded = octstr_create("Basic realm=\"");
			octstr_append(decoded, realm_value);
			octstr_append_char(decoded, '"');
		}
	} else if (val >= 32 && val < 128) {
		/* Generic authentication scheme */
		decoded = parse_get_nul_string(context);
		realm_value = parse_get_nul_string(context);
		if (decoded && realm_value) {
			octstr_append_cstr(decoded, "realm=\"");
			octstr_append(decoded, realm_value);
			octstr_append_char(decoded, '"');
			unpack_all_parameters(context, decoded);
		}
	}

	if (!decoded)
		warning(0, "Cannot parse challenge.");
	
	octstr_destroy(realm_value);
	return decoded;
}

/* Content-range is defined in 8.4.2.23 */
static Octstr *unpack_content_range(Context *context) {
	/* We'd have to figure out how to access the content range
	 * length (i.e. user_data size) from here to parse this,
	 * and I don't see why the _client_ would send this in any case. */
	warning(0, "Decoding of content-range not supported");
	return NULL;

/*
	Octstr *decoded = NULL;
	unsigned long first_byte_pos, entity_length;
	unsigned long last_byte_pos;

	first_byte_pos = parse_get_uintvar(context);
	entity_length = parse_get_uintvar(context);

	if (parse_error(context)) {
		warning(0, "Cannot parse content-range header");
		return NULL;
	}

	decoded = octstr_create("bytes ");
	octstr_append_decimal(decoded, first_byte_pos);
	octstr_append_char(decoded, '-');
	octstr_append_decimal(decoded, last_byte_pos);
	octstr_append_char(decoded, '/');
	octstr_append_decimal(decoded, entity_length);

	return decoded;
*/
}

/* Field-name is defined in 8.4.2.6 */
static Octstr *unpack_field_name(Context *context) {
	Octstr *decoded = NULL;
	int ret;
	int val;

	ret = field_value(context, &val);
	if (parse_error(context) || ret == WSP_FIELD_VALUE_DATA) {
		warning(0, "Bad field-name encoding");
		return NULL;
	}

	if (ret == WSP_FIELD_VALUE_ENCODED) {
		decoded = wsp_header_to_string(val);
		if (!decoded) {
			warning(0, "Unknown field-name 0x%02x.", val);
			return NULL;
		}
	} else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
		decoded = parse_get_nul_string(context);
		if (!decoded) {
			warning(0, "Bad field-name encoding");
			return NULL;
		}
	} else {
		panic(0, "Unknown field value type %d.", ret);
	}

	return decoded;
}

/* Cache-directive is defined in 8.4.2.15 */
static Octstr *unpack_cache_directive(Context *context) {
	Octstr *decoded = NULL;
	int ret;
	int val;

	ret = field_value(context, &val);
	if (parse_error(context) || ret == WSP_FIELD_VALUE_DATA) {
		warning(0, "Bad cache-directive");
		goto error;
	}

	if (ret == WSP_FIELD_VALUE_ENCODED) {
		decoded = wsp_cache_control_to_string(val);
		if (!decoded) {
			warning(0, "Bad cache-directive 0x%02x.", val);
			goto error;
		}
		octstr_append_char(decoded, '=');
		switch (val) {
		case WSP_CACHE_CONTROL_NO_CACHE:
		case WSP_CACHE_CONTROL_PRIVATE:
			if (parse_octets_left(context) == 0) {
				warning(0, "Too short cache-directive");
				goto error;
			}
			do {
				Octstr *fieldname = unpack_field_name(context);
				if (!fieldname) {
					warning(0, "Bad field name in cache directive");
					goto error;
				}
				octstr_append(decoded, fieldname);
				octstr_destroy(fieldname);
				if (parse_octets_left(context) > 0)
					octstr_append_cstr(decoded, ", ");
			} while (parse_octets_left(context) > 0 &&
				!parse_error(context));
			break;
		case WSP_CACHE_CONTROL_MAX_AGE:
		case WSP_CACHE_CONTROL_MAX_STALE:
		case WSP_CACHE_CONTROL_MIN_FRESH:
			{
				Octstr *seconds;
				seconds = unpack_integer_value(context);
				if (!seconds) {
					warning(0, "Bad integer value in cache directive");
					goto error;
				}
				octstr_append(decoded, seconds);
				octstr_destroy(seconds);
			}
			break;
		default:
			warning(0, "Unexpected value 0x%02x in cache directive.", val);
			break;
		}
	} else if (ret == WSP_FIELD_VALUE_NUL_STRING) {
		decoded = parse_get_nul_string(context);
		if (!decoded) {
			warning(0, "Format erorr in cache-control.");
			return NULL;
		}
		/* Yes, the grammar allows only one */
		unpack_parameter(context, decoded);
	} else {
		panic(0, "Unknown field value type %d.", ret);
	}

	return decoded;

error:
	octstr_destroy(decoded);
	return NULL;
}

/* Retry-after is defined in 8.4.2.44 */
static Octstr *unpack_retry_after(Context *context) {
	int selector;

	selector = parse_get_char(context);
	if (selector == 128) {
		/* Absolute-time */
		return unpack_date_value(context);
	} else if (selector == 129) {
		/* Relative-time */
		return unpack_integer_value(context);
	} else  {
		warning(0, "Cannot parse retry-after value.");
		return NULL;
	}
}

/* Disposition is defined in 8.4.2.53 */
static Octstr *unpack_disposition(Context *context) {
	Octstr *decoded = NULL;
	int selector;

	selector = parse_get_char(context) - 128;
	decoded = wsp_disposition_to_string(selector);
	if (!decoded) {
		warning(0, "Cannot parse content-disposition value.");
		return NULL;
	}
	unpack_all_parameters(context, decoded);
	return decoded;
}

/* Range-value is defined in 8.4.2.42 */
static Octstr *unpack_range_value(Context *context) {
	Octstr *decoded = NULL;
	int selector;
	unsigned long first_byte_pos, last_byte_pos, suffix_length;

	selector = parse_get_char(context);
	if (selector == 128) {
		/* Byte-range */
		first_byte_pos = parse_get_uintvar(context);
		if (parse_error(context))
			goto error;

		decoded = octstr_create("bytes = ");
		octstr_append_decimal(decoded, first_byte_pos);
		octstr_append_char(decoded, '-');

		last_byte_pos = parse_get_uintvar(context);
		if (parse_error(context)) {
			/* last_byte_pos is optional */
			parse_clear_error(context);
		} else {
			octstr_append_decimal(decoded, last_byte_pos);
		}
	} else if (selector == 129) {
		/* Suffix-byte-range */
		suffix_length = parse_get_uintvar(context);
		if (parse_error(context))
			goto error;

		decoded = octstr_create("bytes = -");
		octstr_append_decimal(decoded, suffix_length);
	} else {
		goto error;
	}

	return decoded;

error:
	warning(0, "Bad format for range-value.");
	octstr_destroy(decoded);
	return NULL;
}

/* Warning-value is defined in 8.4.2.51 */
static Octstr *unpack_warning_value(Context *context) {
	Octstr *decoded = NULL;
	unsigned long warn_code;
	Octstr *warn_agent = NULL;
	Octstr *warn_text = NULL;

	warn_code = parse_get_char(context) - 128;
	if (warn_code < 0 || warn_code > 99)
		goto error;

	warn_agent = parse_get_nul_string(context);
	if (warn_agent && octstr_get_char(warn_agent, 0) == WSP_QUOTE)
		octstr_delete(warn_agent, 0, 1);

	warn_text = parse_get_nul_string(context);
	if (warn_text && octstr_get_char(warn_text, 0) == WSP_QUOTE)
		octstr_delete(warn_text, 0, 1);

	if (parse_error(context) || !warn_agent || !warn_text)
		goto error;

	decoded = octstr_create("");
	octstr_append_decimal(decoded, warn_code);
	octstr_append_char(decoded, ' ');
	octstr_append(decoded, warn_agent);
	octstr_append_char(decoded, ' ');
	octstr_append_char(decoded, '"');
	octstr_append(decoded, warn_text);
	octstr_append_char(decoded, '"');
	
	octstr_destroy(warn_agent);
	octstr_destroy(warn_text);
	return decoded;

error:
	warning(0, "Bad format for warning-value.");
	octstr_destroy(warn_agent);
	octstr_destroy(warn_text);
	octstr_destroy(decoded);
	return NULL;
}

static void unpack_well_known_field(List *unpacked, int field_type, Context *context) {
	int val, ret;
	unsigned char *headername = NULL;
	unsigned char *ch = NULL;
	Octstr *decoded = NULL;

	ret = field_value(context, &val);
	if (parse_error(context)) {
		warning(0, "Faulty header, skipping remaining headers.");
		parse_skip_to_limit(context);
		return;
	}

	headername = wsp_header_to_cstr(field_type);
	/* headername can still be NULL.  This is checked after parsing
	 * the field value.  We want to parse the value before exiting,
	 * so that we are ready for the next header. */

	/* The following code must set "ch" or "decoded" to a non-NULL
	 * value if the header is valid. */

	if (ret == WSP_FIELD_VALUE_NUL_STRING) {
		/* We allow any header to have a text value, even if that
		 * is not defined in the grammar.  Be generous in what
		 * you accept, etc. */
		/* This covers Text-string, Token-Text, and Uri-value rules */
		decoded = parse_get_nul_string(context);
	} else if (ret == WSP_FIELD_VALUE_ENCODED) {
		switch (field_type) {
		case WSP_HEADER_ACCEPT:
		case WSP_HEADER_CONTENT_TYPE:
			ch = wsp_content_type_to_cstr(val);
			if (!ch)
				warning(0, "Unknown content type 0x%02x.", val);
			break;

		case WSP_HEADER_ACCEPT_CHARSET:
			ch = wsp_charset_to_cstr(val);
			if (!ch)
				warning(0, "Unknown charset 0x%02x.", val);
		    	break;

		case WSP_HEADER_ACCEPT_ENCODING:
		case WSP_HEADER_CONTENT_ENCODING:
			ch = wsp_encoding_to_cstr(val);
			if (!ch)
				warning(0, "Unknown encoding 0x%02x.", val);
			break;
	
		case WSP_HEADER_ACCEPT_LANGUAGE:
		case WSP_HEADER_CONTENT_LANGUAGE:
			ch = wsp_language_to_cstr(val);
			if (!ch)
				warning(0, "Unknown language 0x%02x.", val);
			break;

		case WSP_HEADER_ACCEPT_RANGES:
			ch = wsp_ranges_to_cstr(val);
			if (!ch)
				warning(0, "Unknown ranges value 0x%02x.", val);
			break;

		case WSP_HEADER_AGE:
		case WSP_HEADER_CONTENT_LENGTH:
		case WSP_HEADER_MAX_FORWARDS:
			/* Short-integer version of Integer-value */
			decoded = octstr_create("");
			octstr_append_decimal(decoded, val);
			break;

		case WSP_HEADER_ALLOW:
		case WSP_HEADER_PUBLIC:
			ch = wsp_method_to_cstr(val);
			if (!ch) {
				/* XXX Support extended methods */
				warning(0, "Unknown method 0x%02x.", val);
			}
			break;

		case WSP_HEADER_CACHE_CONTROL:
			ch = wsp_cache_control_to_cstr(val);
			if (!ch) 
				warning(0, "Unknown cache-control value 0x%02x.", val);
			break;

		case WSP_HEADER_CONNECTION:
			if (val == 0)
				ch = "close";
			else
				warning(0, "Unknown connection value 0x%02x.", val);
			break;


		case WSP_HEADER_PRAGMA:
			if (val == 0)
				ch = "no-cache";
			else
				warning(0, "Unknown pragma value 0x%02x.", val);
			break;

		case WSP_HEADER_TRANSFER_ENCODING:
			if (val == 0)
				ch = "chunked";
			else
				warning(0, "Unknown transfer encoding value 0x%02x.", val);
			break;

		case WSP_HEADER_VARY:
			ch = wsp_header_to_cstr(val);
			if (!ch)
				warning(0, "Unknown Vary field name 0x%02x.", val);
			break;

		case WSP_HEADER_WARNING:
			decoded = octstr_create("");
			octstr_append_decimal(decoded, val);
			break;

		default:
			if (headername) {
				warning(0, "Did not expect short-integer with "
				"'%s' header, skipping.", headername);
			}
			break;
		}
	} else if (ret == WSP_FIELD_VALUE_DATA) {
		switch (field_type) {
		case WSP_HEADER_ACCEPT:
		case WSP_HEADER_CONTENT_TYPE:
			/* Content-general-form and Accept-general-form
			 * are defined separately in WSP, but their
			 * definitions are equivalent. */
			decoded = unpack_accept_general_form(context);
			break;

		case WSP_HEADER_ACCEPT_CHARSET:
			decoded = unpack_accept_charset_general_form(context);
		    	break;

		case WSP_HEADER_ACCEPT_LANGUAGE:
			decoded = unpack_accept_language_general_form(context);
			break;

		case WSP_HEADER_AGE:
		case WSP_HEADER_CONTENT_LENGTH:
		case WSP_HEADER_MAX_FORWARDS:
			/* Long-integer version of Integer-value */
			{
				long l = unpack_multi_octet_integer(context,
					parse_octets_left(context));
				decoded = octstr_create("");
				octstr_append_decimal(decoded, l);
			}
			break;

		case WSP_HEADER_AUTHORIZATION:
		case WSP_HEADER_PROXY_AUTHORIZATION:
			decoded = unpack_credentials(context);
			break;

		case WSP_HEADER_CACHE_CONTROL:
			decoded = unpack_cache_directive(context);
			break;

		case WSP_HEADER_CONTENT_MD5:
			decoded = parse_get_octets(context,
					parse_octets_left(context));
			octstr_binary_to_base64(decoded);
			/* Zap the CR LF sequence at the end */
			octstr_delete(decoded, octstr_len(decoded) - 2, 2);
			break;

		case WSP_HEADER_CONTENT_RANGE:
			decoded = unpack_content_range(context);
			break;

		case WSP_HEADER_DATE:
		case WSP_HEADER_EXPIRES:
		case WSP_HEADER_IF_MODIFIED_SINCE:
		case WSP_HEADER_IF_RANGE:
		case WSP_HEADER_IF_UNMODIFIED_SINCE:
		case WSP_HEADER_LAST_MODIFIED:
			decoded = unpack_date_value(context);
			break;

		case WSP_HEADER_PRAGMA:
			/* The value is a bare Parameter, without a preceding
			 * header body.  unpack_parameter wasn't really
			 * designed for this.  We work around it here. */
			decoded = octstr_create("");
			if (unpack_parameter(context, decoded) < 0) {
				octstr_destroy(decoded);
				decoded = NULL;
			} else {
				/* Remove the leading "; " */
				octstr_delete(decoded, 0, 2);
			}
			break;

		case WSP_HEADER_PROXY_AUTHENTICATE:
		case WSP_HEADER_WWW_AUTHENTICATE:
			decoded = unpack_challenge(context);
			break;

		case WSP_HEADER_RANGE:
			decoded = unpack_range_value(context);
			break;

		case WSP_HEADER_RETRY_AFTER:
			decoded = unpack_retry_after(context);
			break;

		case WSP_HEADER_WARNING:
			decoded = unpack_warning_value(context);
			break;

		case WSP_HEADER_CONTENT_DISPOSITION:
			decoded = unpack_disposition(context);
			break;

		default:
			if (headername) {
				warning(0, "Did not expect value-length with "
				"'%s' header, skipping.", headername);
			}
			break;
		}
		if (headername && parse_octets_left(context) > 0) {
			warning(0, "WSP: %s: skipping %ld trailing octets.",
				headername, parse_octets_left(context));
		}
		parse_skip_to_limit(context);
		parse_pop_limit(context);
	} else {
		panic(0, "Unknown field-value type %d.", ret);
	}

	if (ch == NULL && decoded != NULL)
		ch = octstr_get_cstr(decoded);
	if (ch == NULL)
		goto value_error;

	if (!headername) {
		warning(0, "Unknown header number 0x%02x.", field_type);
		goto value_error;
	}

	http_header_add(unpacked, headername, ch);
	octstr_destroy(decoded);
	return;

value_error:
	warning(0, "Skipping faulty header.");
	octstr_destroy(decoded);
}

static void unpack_app_header(List *unpacked, Context *context) {
	Octstr *header = NULL;
	Octstr *value = NULL;

	header = parse_get_nul_string(context);
	value = parse_get_nul_string(context);

	if (header && value) {
		http_header_add(unpacked, octstr_get_cstr(header),
					  octstr_get_cstr(value));
	}

	if (parse_error(context))
		warning(0, "Error parsing application-header.");

	octstr_destroy(header);
	octstr_destroy(value);
}

List *unpack_headers(Octstr *headers, int content_type_present) {
	Context *context;
	int byte;
    	List *unpacked;

	unpacked = http_create_empty_headers();
	context = parse_context_create(headers);

	if (octstr_len(headers) > 0) {
		debug("wsp", 0, "WSP: decoding headers:"); 
		octstr_dump(headers, 0);
	}

	if (content_type_present)
		unpack_well_known_field(unpacked,
				WSP_HEADER_CONTENT_TYPE, context);

	while (parse_octets_left(context) > 0 && !parse_error(context)) {
		byte = parse_get_char(context);
	
		if (byte == 127) {
			warning(0, "Ignoring shift-delimiter");
			parse_skip(context, 1); /* ignore page-identity */
		} else if (byte >= 1 && byte <= 31) {
			warning(0, "Ignoring short-cut-shift-delimiter %d.",
					byte);
		} else if (byte >= 128) {  /* well-known-header */
			unpack_well_known_field(unpacked, byte-128, context);
		} else if (byte > 31 && byte < 127) {
			/* Un-parse the character we just read */
			parse_skip(context, -1);
			unpack_app_header(unpacked, context);
		} else {
			warning(0, "Unsupported token or header (start 0x%x)", byte);
			break;
		}
	}

	if (list_len(unpacked) > 0) {
		long i;

		debug("wsp", 0, "WSP: decoded headers:");
		for (i = 0; i < list_len(unpacked); i++) {
			Octstr *header = list_get(unpacked, i);
			debug("wsp", 0, octstr_get_cstr(header));
		}
		debug("wsp", 0, "WSP: End of decoded headers.");
	}
		
	parse_context_destroy(context);
	return unpacked;
}	
