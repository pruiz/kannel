/*
 * html.c - routines for manipulating HTML.
 *
 * Lars Wirzenius for WapIT Ltd.
 */


#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "html.h"
#include "gwlib/gwlib.h"

#define SMS_MAX 161

/* Skip a comment in HTML. */
static char *skip_html_comment(char *html) {
	html += 4;	/* Skip "<!--" at beginning of comment. */
	html = strstr(html, "-->");
	if (html == NULL)
		return "";
	return html + 3;
}


/* Skip a beginning of ending tag in HTML, including any attributes. */
static char *skip_html_tag(char *html) {
	/* Skip leading '<'. */
	++html;

	/* Skip name of tag and attributes with values. */
	while (*html != '\0' && *html != '>') {
		if (*html == '"' || *html == '\'') {
			html = strchr(html + 1, *html);
			if (html == NULL)
				html = "";
			else
				++html;
		} else
			++html;
	}
	
	/* Skip trailing '>' if it is there. */
	if (*html == '>')
		++html;

	return html;
}


/* Convert an HTML entity into a single character and advance `*html' past
   the entity. */
static int convert_html_entity(char **html) {
	static struct {
		char *entity;
		int character;
	} tab[] = {
		{ "&amp;", '&' },
		{ "&lt;", '<' },
		{ "&gt;", '>' },

		/* The following is copied from 

			http://www.hut.fi/~jkorpela/HTML3.2/latin1.html
		
		   by Jukka Korpela. Hand and script edited to form this
		   table. */

		{ "&nbsp;", ' ' },
		{ "&iexcl;", 161 },
		{ "&cent;", 162 },
		{ "&pound;", 163 },
		{ "&curren;", 164 },
		{ "&yen;", 165 },
		{ "&brvbar;", 166 },
		{ "&sect;", 167 },
		{ "&uml;", 168 },
		{ "&copy;", 169 },
		{ "&ordf;", 170 },
		{ "&laquo;", 171 },
		{ "&not;", 172 },
		{ "&shy;", 173 },
		{ "&reg;", 174 },
		{ "&macr;", 175 },
		{ "&deg;", 176 },
		{ "&plusmn;", 177 },
		{ "&sup2;", 178 },
		{ "&sup3;", 179 },
		{ "&acute;", 180 },
		{ "&micro;", 181 },
		{ "&para;", 182 },
		{ "&middot;", 183 },
		{ "&cedil;", 184 },
		{ "&sup1;", 185 },
		{ "&ordm;", 186 },
		{ "&raquo;", 187 },
		{ "&frac14;", 188 },
		{ "&frac12;", 189 },
		{ "&frac34;", 190 },
		{ "&iquest;", 191 },
		{ "&Agrave;", 192 },
		{ "&Aacute;", 193 },
		{ "&Acirc;", 194 },
		{ "&Atilde;", 195 },
		{ "&Auml;", 196 },
		{ "&Aring;", 197 },
		{ "&AElig;", 198 },
		{ "&Ccedil;", 199 },
		{ "&Egrave;", 200 },
		{ "&Eacute;", 201 },
		{ "&Ecirc;", 202 },
		{ "&Euml;", 203 },
		{ "&Igrave;", 204 },
		{ "&Iacute;", 205 },
		{ "&Icirc;", 206 },
		{ "&Iuml;", 207 },
		{ "&ETH;", 208 },
		{ "&Ntilde;", 209 },
		{ "&Ograve;", 210 },
		{ "&Oacute;", 211 },
		{ "&Ocirc;", 212 },
		{ "&Otilde;", 213 },
		{ "&Ouml;", 214 },
		{ "&times;", 215 },
		{ "&Oslash;", 216 },
		{ "&Ugrave;", 217 },
		{ "&Uacute;", 218 },
		{ "&Ucirc;", 219 },
		{ "&Uuml;", 220 },
		{ "&Yacute;", 221 },
		{ "&THORN;", 222 },
		{ "&szlig;", 223 },
		{ "&agrave;", 224 },
		{ "&aacute;", 225 },
		{ "&acirc;", 226 },
		{ "&atilde;", 227 },
		{ "&auml;", 228 },
		{ "&aring;", 229 },
		{ "&aelig;", 230 },
		{ "&ccedil;", 231 },
		{ "&egrave;", 232 },
		{ "&eacute;", 233 },
		{ "&ecirc;", 234 },
		{ "&euml;", 235 },
		{ "&igrave;", 236 },
		{ "&iacute;", 237 },
		{ "&icirc;", 238 },
		{ "&iuml;", 239 },
		{ "&eth;", 240 },
		{ "&ntilde;", 241 },
		{ "&ograve;", 242 },
		{ "&oacute;", 243 },
		{ "&ocirc;", 244 },
		{ "&otilde;", 245 },
		{ "&ouml;", 246 },
		{ "&divide;", 247 },
		{ "&oslash;", 248 },
		{ "&ugrave;", 249 },
		{ "&uacute;", 250 },
		{ "&ucirc;", 251 },
		{ "&uuml;", 252 },
		{ "&yacute;", 253 },
		{ "&thorn;", 254 },
		{ "&yuml;", 255 },
	};
	int num_tab = sizeof(tab) / sizeof(tab[0]);
	int i, code, digits;
	size_t len;
	
	if ((*html)[1] == '#') {
		digits = strspn((*html) + 2, "0123456789");
		code = 0;
		for (i = 0; i < digits; ++i)
			code = code * 10 + (*html)[2 + i] - '0';
		if (code <= ' ')
			code = ' ';
		*html += 2 + digits;
		if (**html == ';')
			++(*html);
		return code;
	}

	for (i = 0; i < num_tab; ++i) {
		len = strlen(tab[i].entity);
		if (strncmp(*html, tab[i].entity, len) == 0) {
			*html += len;
			return tab[i].character;
		}
	}

	++(*html);
	return '&';
}


/* Convert an HTML page into an SMS message: Remove tags, convert entities,
   and so on. */
void html_to_sms(char *sms, size_t smsmax_u, char *html_arg) {
	int n;
	long smsmax;
	unsigned char *html;
	
	html = html_arg;
	n = 0;
	smsmax = smsmax_u;
	while (n < smsmax-1 && *html != '\0') {
		switch (*html) {
		case '<':
			if (strncmp(html, "<!--", 4) == 0)
				html = skip_html_comment(html);
			else
				html = skip_html_tag(html);
			break;
		case '&':
			sms[n++] = convert_html_entity((char **) &html);
			break;
		default:
			if (isspace(*html)) {
				if (n > 0 && sms[n-1] != ' ')
					sms[n++] = ' ';
				++html;
			} else
				sms[n++] = *(html++);
			break;
		}
	}
	while (n > 0 && sms[n-1] == ' ')
		--n;
	sms[n] = '\0';
}


char *html_strip_prefix_and_suffix(char *html, char *prefix, char *suffix) {
	char *p, *q;

	p = str_case_str(html, prefix);
	if (p == NULL)
		return gw_strdup(html);	/* return original, if no prefix */
	p += strlen(prefix);
	q = str_case_str(p, suffix);
	if (q == NULL)
		return gw_strdup(html);	/* return original, if no suffix */
	return strndup(p, q - p);
}
