/*
 * wsp_http.c - The HTTP fetching and document processing thread
 *
 * Lars Wirzenius <liw@wapit.com>
 * Capabilities/headers by Kalle Marjola <rpr@wapit.com>
 * URL mapping by Patrick Schaaf <bof@bof.de>
 */

#include <string.h>

#include "gwlib/gwlib.h"
#include "wmlscript/ws.h"
#include "wsp.h"
#include "wml_compiler.h"

static void  dev_null(const char *data, size_t len, void *context);
static int encode_content_type(const char *type);

static Octstr *convert_to_self(Octstr *stuff, Octstr *charset, char *url);
static Octstr *convert_wml_to_wmlc(Octstr *wml, Octstr *charset, char *url);
static Octstr *convert_wmlscript_to_wmlscriptc(Octstr *wmlscript, 
					       Octstr *charset, char *url);

/* The following code implements the map-url mechanism */

struct wsp_http_map {
	struct wsp_http_map *next;
	unsigned flags;
#define WSP_HTTP_MAP_INPREFIX	0x0001	/* prefix-match incoming string */
#define WSP_HTTP_MAP_OUTPREFIX	0x0002	/* prefix-replace outgoing string */
#define WSP_HTTP_MAP_INOUTPREFIX 0x0003	/* combine the two for masking */
	char *in;
	int in_len;
	char *out;
	int out_len;
};

static struct wsp_http_map *wsp_http_map = 0;
static struct wsp_http_map *wsp_http_map_last = 0;

/* Add mapping for src URL to dst URL. */
static void wsp_http_map_url_do_config(char *src, char *dst)
{
	struct wsp_http_map *new_map;
	int in_len = src ? strlen(src) : 0;
	int out_len = dst ? strlen(dst) : 0;

	if (!in_len) {
		warning(0, "wsp_http_map_url_do_config: empty incoming string");
		return;
	}
	gw_assert(in_len > 0);

	new_map = gw_malloc(sizeof(*new_map));
	new_map->next = NULL;
	new_map->flags = 0;

	/* incoming string
	 * later, the incoming URL will be prefix-compared to new_map->in,
	 * using exactly new_map->in_len characters for comparison.
	 */
	new_map->in = gw_strdup(src);
	if (src[in_len-1] == '*') {
		new_map->flags |= WSP_HTTP_MAP_INPREFIX;
		in_len--;		/* do not include `*' in comparison */
	} else {
		in_len++;		/* include \0 in comparisons */
	}
	new_map->in_len = in_len;

	/* replacement string
	 * later, when an incoming URL matches, it will be replaced
	 * or modified according to this string. If the replacement
	 * string ends with an asterisk, and the match string indicates
	 * a prefix match (also ends with an asterisk), the trailing
	 * part of the matching URL will be appended to the replacement
	 * string, i.e. we do a prefix replacement.
	 */
	new_map->out = gw_strdup(dst);
	if (dst[out_len-1] == '*') {
		new_map->flags |= WSP_HTTP_MAP_OUTPREFIX;
		out_len--;			/* exclude `*' */
	}
	new_map->out_len = out_len;

	/* insert at tail of existing list */
	if (wsp_http_map == NULL) {
		wsp_http_map = wsp_http_map_last = new_map;
	} else {
		wsp_http_map_last->next = new_map;
		wsp_http_map_last = new_map;
	}
}

/* Called during configuration read, once for each "map-url" statement.
 * Interprets parameter value as a space-separated two-tuple of src and dst.
 */
void wsp_http_map_url_config(char *s)
{
	char *in, *out;

	s = gw_strdup(s);
	in = strtok(s, " \t");
	if (!in) return;
	out = strtok(NULL, " \t");
	if (!out) return;
	wsp_http_map_url_do_config(in, out);
	gw_free(s);
}

/* Called during configuration read, this adds a mapping for the source URL
 * "DEVICE:home", to the given destination. The mapping is configured
 * as an in/out prefix mapping.
 */
void wsp_http_map_url_config_device_home(char *to)
{
	int len;
	char *newto = 0;

	if (!to)
		return;
	len = strlen(to);
	if (to[len] != '*') {
		newto = gw_malloc(len+2);
		strcpy(newto, to);
		newto[len] = '*';
		newto[len+1] = '\0';
		to = newto;
	}
	wsp_http_map_url_do_config("DEVICE:home*", to);
	if (newto)
		gw_free(newto);
}

/* show mapping list at info level, after configuration is done. */
void wsp_http_map_url_config_info(void)
{
	struct wsp_http_map *run;

	for (run = wsp_http_map; run; run = run->next) {
		char *s1 = (run->flags & WSP_HTTP_MAP_INPREFIX)  ? "*" : "";
		char *s2 = (run->flags & WSP_HTTP_MAP_OUTPREFIX) ? "*" : "";
		info(0, "map-url %.*s%s %.*s%s",
			run->in_len, run->in, s1,
			run->out_len, run->out, s2);
	}
}

/* Search list of mappings for the given URL, returning the map structure. */
static struct wsp_http_map *wsp_http_map_find(char *s)
{
	struct wsp_http_map *run;

	for (run = wsp_http_map; run; run = run->next)
		if (0 == strncasecmp(s, run->in, run->in_len))
			break;
	if (run) {
		debug("wap.wsp.http", 0, "WSP: found mapping for url <%s>", s);
	}
	return run;
}

/* Maybe rewrite URL, if there is a mapping. This is where the runtime
 * lookup comes in (called from further down this file, wsp_http.c)
 */
static void wsp_http_map_url(Octstr **osp)
{
	struct wsp_http_map *map;
	Octstr *old = *osp;
	char *oldstr = octstr_get_cstr(old);

	map = wsp_http_map_find(oldstr);
	if (!map)
		return;
	*osp = octstr_create_from_data(map->out, map->out_len);
	/* 
	 * If both prefix flags are set, append tail of incoming URL
	 * to outgoing URL.
	 */
	if (WSP_HTTP_MAP_INOUTPREFIX == (map->flags & WSP_HTTP_MAP_INOUTPREFIX))
		octstr_append_cstr(*osp, oldstr + map->in_len);
	debug("wap.wsp.http", 0, "WSP: url <%s> mapped to <%s>",
		oldstr, octstr_get_cstr(*osp));
	octstr_destroy(old);
}

/* here comes the main processing */

void *wsp_http_thread(void *arg) {
	WSPEvent *e;
	int status;
	int ret;
	int i;
	int converter_failed;
	WSPEvent *event;
	unsigned long body_size, client_SDU_size;
	WTPMachine *wtp_sm;
	WSPMachine *sm;
	HTTPHeader *h;
	int wml_ok, wmlc_ok, wmlscript_ok, wmlscriptc_ok;
	Octstr *url, *final_url, *resp_body, *body, *os, *type, *charset;
	List *req_headers, *resp_headers;
	
	static struct {
		char *type;
		char *result_type;
		Octstr *(*convert)(Octstr *, Octstr *, char *);
	} converters[] = {
		{ "text/vnd.wap.wml",
		  "application/vnd.wap.wmlc",
		  convert_wml_to_wmlc },
		{ "application/vnd.wap.wmlc",
		  "application/vnd.wap.wmlc",
		  convert_to_self },
		{ "image/vnd.wap.wbmp",
		  "image/vnd.wap.wbmp",
		  convert_to_self },
		{ "text/vnd.wap.wmlscript",
		  "application/vnd.wap.wmlscriptc",
		  convert_wmlscript_to_wmlscriptc },
		{ "",
		  "",
		  convert_to_self },
		{ "",
		  "",
		  convert_to_self },
		{ "",
		  "",
		  convert_to_self },
	};
	static int num_converters = sizeof(converters) / sizeof(converters[0]);

	event = arg;
	wtp_sm = event->S_MethodInvoke_Res.machine;
	sm = event->S_MethodInvoke_Res.session;
	wsp_dispatch_event(wtp_sm, wsp_event_duplicate(event));
		/* XXX shouldn't this duplicate the event? */

	wsp_http_map_url(&event->S_MethodInvoke_Res.url);
	url = event->S_MethodInvoke_Res.url;

	body = NULL;

	wml_ok = 0;
	wmlc_ok = 0;
	wmlscript_ok = 0;
	wmlscriptc_ok = 0;
	
	req_headers = list_create();
	for (h = sm->http_headers; h != NULL; h = h->next) {
		os = octstr_create(h->key);
		octstr_append_cstr(os, ": ");
		octstr_append_cstr(os, h->value);
		list_append(req_headers, os);
		if (strcasecmp(h->key, "Accept")) {
			if (strstr(h->value, "text/vnd.wap.wml") != NULL)
				wml_ok = 1;
			if (strstr(h->value, "text/vnd.wap.wmlscript") != NULL)
				wmlscript_ok = 1;
			if (strstr(h->value, "application/vnd.wap.wmlc") != NULL)
				wmlc_ok = 1;
			if (strstr(h->value, "application/vnd.wap.wmlscriptc") != NULL)
				wmlscriptc_ok = 1;
		}
	}
	for (h = event->S_MethodInvoke_Res.http_headers; h != NULL; h = h->next) {
		os = octstr_create(h->key);
		octstr_append_cstr(os, ": ");
		octstr_append_cstr(os, h->value);
		list_append(req_headers, os);
		if (strcasecmp(h->key, "Accept")) {
			if (strstr(h->value, "text/vnd.wap.wml") != NULL)
				wml_ok = 1;
			if (strstr(h->value, "text/vnd.wap.wmlscript") != NULL)
				wmlscript_ok = 1;
			if (strstr(h->value, "application/vnd.wap.wmlc") != NULL)
				wmlc_ok = 1;
			if (strstr(h->value, "application/vnd.wap.wmlscriptc") != NULL)
				wmlscriptc_ok = 1;
		}
	}

	if (wmlc_ok && !wml_ok) {
		list_append(req_headers, 
			octstr_create("Accept: text/vnd.wap.wml"));
	}
	if (wmlscriptc_ok && !wmlscript_ok) {
		list_append(req_headers, 
			octstr_create("Accept: text/vnd.wap.wmlscript"));
	}
	if (octstr_len(sm->client_address) > 0) {
		os = octstr_create("X_Network_Info: ");
		octstr_append_cstr(os, octstr_get_cstr(sm->client_address));
		list_append(req_headers, os);
	}
	{
		char buf[1024];
		
		os = octstr_create("X-WAP-Session-ID: ");
		sprintf(buf, "%ld", sm->session_id);
		octstr_append_cstr(os, buf);
		list_append(req_headers, os);
	}

	ret = http2_get_real(url, req_headers, 
			     &final_url, &resp_headers, &resp_body);
	octstr_destroy(final_url);

	if (ret != HTTP_OK) {
		error(0, "WSP: http2_get_real failed, oops.");
		status = 500; /* Internal server error; XXX should be 503 */
		type = octstr_create("text/plain");
	} else {
		http2_header_get_content_type(resp_headers, &type, &charset);
		info(0, "WSP: Fetched <%s> (%s)", 
			octstr_get_cstr(url), octstr_get_cstr(type));
		status = 200; /* OK */
		
		converter_failed = 0;
		for (i = 0; i < num_converters; ++i) {
			if (octstr_str_compare(type, converters[i].type) == 0) {
				body = converters[i].convert(resp_body, 
					charset,
					octstr_get_cstr(url));
				if (body != NULL)
					break;
				converter_failed = 1;
			}
		}

		if (i < num_converters) {
			octstr_destroy(type);
			type = octstr_create(converters[i].result_type);
		} else if (converter_failed) {
			status = 500; /* XXX */
			warning(0, "WSP: All converters for `%s' failed.",
					octstr_get_cstr(type));
		} else {
			status = 415; /* Unsupported media type */
			warning(0, "WSP: Unsupported content type `%s'", 
				octstr_get_cstr(type));
			debug("wap.wsp.http", 0, 
				"Content of unsupported content:");
			octstr_dump(resp_body, 0);
		}
		octstr_destroy(charset);
	}
	octstr_destroy(resp_body);
	gw_assert(req_headers);
	while ((os = list_extract_first(req_headers)) != NULL)
		octstr_destroy(os);
	list_destroy(req_headers);

	gw_assert(resp_headers);
	while ((os = list_extract_first(resp_headers)) != NULL)
		octstr_destroy(os);
	list_destroy(resp_headers);
		
	if (body == NULL)
		body_size = 0;
	else
		body_size = octstr_len(body);
	client_SDU_size = event->S_MethodInvoke_Res.session->client_SDU_size;

	if (body != NULL && body_size > client_SDU_size) {
		status = 413; /* XXX requested entity too large */
		warning(0, "WSP: Entity at %s too large (size %lu B, limit %lu B)",
			octstr_get_cstr(url), body_size, client_SDU_size);
                octstr_destroy(body);
		body = NULL;
		octstr_destroy(type);
		type = octstr_create("text/plain");
	}

	e = wsp_event_create(S_MethodResult_Req);
	e->S_MethodResult_Req.server_transaction_id = 
		event->S_MethodInvoke_Res.server_transaction_id;
	e->S_MethodResult_Req.status = status;
	e->S_MethodResult_Req.response_type = 
		encode_content_type(octstr_get_cstr(type));
	e->S_MethodResult_Req.response_body = body;
	e->S_MethodResult_Req.machine = event->S_MethodInvoke_Res.machine;

	wsp_dispatch_event(event->S_MethodInvoke_Res.machine, e);

	wsp_event_destroy(event);
	octstr_destroy(type);

	return NULL;
}



/* Shut up WMLScript compiler status/trace messages. */
static void dev_null(const char *data, size_t len, void *context) {
  /* nothing */
}


static int encode_content_type(const char *type) {
	static struct {
		char *type;
		int shortint;
	} tab[] = {
		{ "text/plain", 0x03 },
		{ "text/vnd.wap.wml", 0x08 },
		{ "text/vnd.wap.wmlscript", 0x09 },
		{ "application/vnd.wap.wmlc", 0x14 },
		{ "application/vnd.wap.wmlscriptc", 0x15 },
		{ "image/vnd.wap.wbmp", 0x21 },
	};
	int num_items = sizeof(tab) / sizeof(tab[0]);
	int i;
	
	for (i = 0; i < num_items; ++i)
		if (strcmp(type, tab[i].type) == 0)
			return tab[i].shortint;
	error(0, "WSP: Unknown content type <%s>, assuming text/plain.", type);
	return 0x03;
}


static Octstr *convert_to_self(Octstr *stuff, Octstr *charset, char *url) {
	return octstr_duplicate(stuff);
}


static Octstr *convert_wml_to_wmlc(Octstr *wml, Octstr *charset, char *url) {
	Octstr *wmlc;
	int ret;

	ret = wml_compile(wml, charset, &wmlc);
	if (ret == 0)
		return wmlc;
	warning(0, "WSP: WML compilation failed.");
	return NULL;
}


static Octstr *convert_wmlscript_to_wmlscriptc(Octstr *wmlscript, 
					       Octstr *charset, char *url) {
	WsCompilerParams params;
	WsCompilerPtr compiler;
	WsResult result;
	unsigned char *result_data;
	size_t result_size;
	Octstr *wmlscriptc;

	memset(&params, 0, sizeof(params));
	params.use_latin1_strings = 0;
	params.print_symbolic_assembler = 0;
	params.print_assembler = 0;
	params.meta_name_cb = NULL;
	params.meta_name_cb_context = NULL;
	params.meta_http_equiv_cb = NULL;
	params.meta_http_equiv_cb_context = NULL;
	params.stdout_cb = dev_null;
	params.stderr_cb = dev_null;

	compiler = ws_create(&params);
	if (compiler == NULL) {
		panic(0, "WSP: could not create WMLScript compiler");
		exit(1);
	}

	result = ws_compile_data(compiler, 
				 url,
				 octstr_get_cstr(wmlscript),
				 octstr_len(wmlscript),
				 &result_data,
				 &result_size);
	if (result != WS_OK) {
		warning(0, "WSP: WMLScript compilation failed: %s",
			ws_result_to_string(result));
		wmlscriptc = NULL;
	} else {
		wmlscriptc = octstr_create_from_data(result_data, result_size);
	}

	return wmlscriptc;
}
