/*
 * gw/wap-appl.c - wapbox application layer declarations
 *
 * The application layer's outside interface consists of two functions:
 *
 *	wap_appl_start()
 *		This starts the application layer thread.
 *
 *	wap_appl_dispatch(event)
 *		This adds a new event to the application layer event
 *		queue.
 *
 * The application layer is a thread that reads events from its event
 * queue, fetches the corresponding URLs and feeds back events to the
 * WSP layer.
 *
 * Lars Wirzenius
 */

#include <string.h>

#include "gwlib/gwlib.h"
#include "wmlscript/ws.h"
#include "wsp.h"
#include "wml_compiler.h"
#ifdef COOKIE_SUPPORT
#include "cookies.h"
#endif

/*
 * Give the status the module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } run_status = limbo;


/*
 * The queue of incoming events.
 */
static List *queue = NULL;

/*
 * Charsets supported by WML compiler, queried from wml_compiler.
 */
static List *charsets = NULL;


struct content {
	Octstr *body;
	Octstr *type;
	Octstr *charset;
	Octstr *url;
};


/*
 * Private functions.
 */

static void main_thread(void *);
static void fetch_thread(void *);

static void  dev_null(const char *data, size_t len, void *context);

static Octstr *convert_wml_to_wmlc(struct content *content);
static Octstr *convert_wmlscript_to_wmlscriptc(struct content *content);
static void wsp_http_map_url(Octstr **osp);
static List *negotiate_capabilities(List *req_caps);

static struct {
	char *type;
	char *result_type;
	Octstr *(*convert)(struct content *);
} converters[] = {
	{ "text/vnd.wap.wml",
		"application/vnd.wap.wmlc",
		convert_wml_to_wmlc },
	{ "text/vnd.wap.wmlscript",
		"application/vnd.wap.wmlscriptc",
		convert_wmlscript_to_wmlscriptc },
};
#define NUM_CONVERTERS ((long)(sizeof(converters) / sizeof(converters[0])))



/***********************************************************************
 * The public interface to the application layer.
 */

void wap_appl_init(void) {
	gw_assert(run_status == limbo);
	queue = list_create();
	list_add_producer(queue);
	run_status = running;
	charsets = wml_charsets();
	gwthread_create(main_thread, NULL);
}


void wap_appl_shutdown(void) {
	WAPEvent *e;

	gw_assert(run_status == running);
	list_remove_producer(queue);
	run_status = terminating;
	
	gwthread_join_every(main_thread);
	gwthread_join_every(fetch_thread);
	
	while ((e = list_extract_first(queue)) != NULL)
		wap_event_destroy(e);
	list_destroy(queue);
	while (list_len(charsets) > 0) 
		octstr_destroy(list_extract_first(charsets));
	list_destroy(charsets);
}


void wap_appl_dispatch(WAPEvent *event) {
	gw_assert(run_status == running);
	list_produce(queue, event);
}


/***********************************************************************
 * Private functions.
 */


static void main_thread(void *arg) {
	WAPEvent *ind, *res;
	
	while (run_status == running && (ind = list_consume(queue)) != NULL) {
		switch (ind->type) {
		case S_MethodInvoke_Ind:
			res = wap_event_create(S_MethodInvoke_Res);
			res->u.S_MethodInvoke_Res.server_transaction_id =
				ind->u.S_MethodInvoke_Ind.server_transaction_id;
			res->u.S_MethodInvoke_Res.session_id =
				ind->u.S_MethodInvoke_Ind.session_id;
			wsp_session_dispatch_event(res);
			gwthread_create(fetch_thread, ind);
			break;

		case S_Unit_MethodInvoke_Ind:
			gwthread_create(fetch_thread, ind);
			break;

		case S_Connect_Ind:
			res = wap_event_create(S_Connect_Res);
			/* FIXME: Not yet used by WSP layer */
			res->u.S_Connect_Res.server_headers = NULL;
			res->u.S_Connect_Res.negotiated_capabilities =
				negotiate_capabilities(
				ind->u.S_Connect_Ind.requested_capabilities);
			res->u.S_Connect_Res.session_id =
				ind->u.S_Connect_Ind.session_id;
			wsp_session_dispatch_event(res);
			wap_event_destroy(ind);
			break;

		case S_Disconnect_Ind:
			wap_event_destroy(ind);
			break;

		case S_Suspend_Ind:
			wap_event_destroy(ind);
			break;

		case S_Resume_Ind:
			res = wap_event_create(S_Resume_Res);
			/* FIXME: Not yet used by WSP layer */
			res->u.S_Resume_Res.server_headers = NULL;
			res->u.S_Resume_Res.session_id =
				ind->u.S_Resume_Ind.session_id;
			wsp_session_dispatch_event(res);
			wap_event_destroy(ind);
			break;

		case S_MethodResult_Cnf:
			wap_event_destroy(ind);
			break;

		case S_MethodAbort_Ind:
			/* XXX Interrupt the fetch thread somehow */
			wap_event_destroy(ind);
			break;

		default:
			panic(0, "APPL: Can't handle %s event",
				wap_event_name(ind->type));
			break;
		}
	}
}


static int convert_content(struct content *content) {
	Octstr *new_body;
	int failed = 0;
	int i;

	for (i = 0; i < NUM_CONVERTERS; i++) {
		if (octstr_str_compare(content->type, converters[i].type) == 0) {
			new_body = converters[i].convert(content);
			if (new_body != NULL) {
				octstr_destroy(content->body);
				content->body = new_body;
				octstr_destroy(content->type);
				content->type = octstr_create(
					converters[i].result_type);
				return 1;
			}
			failed = 1;
		}
	}

	if (failed)
		return -1;
	return 0;
}


/* Add a header identifying our gateway version */
static void add_kannel_version(List *headers) {
	Octstr *version;

	version = octstr_create("Kannel ");
	octstr_append_cstr(version, VERSION);
	http_header_add(headers, "X-WAP-Gateway", octstr_get_cstr(version));
	octstr_destroy(version);
}


/* Add Accept-Charset: headers for stuff the WML compiler can
 * convert to UTF-8. */
/* XXX This is not really correct, since we will not be able
 * to handle those charsets for all content types, just WML. */
static void add_charset_headers(List *headers) {
	long i, len;

	gw_assert(charsets != NULL);
	len = list_len(charsets);
	for (i = 0; i < len; i++) {
		unsigned char *charset = octstr_get_cstr(list_get(charsets, i));
		if (!http_charset_accepted(headers, charset))
			http_header_add(headers, "Accept-Charset", charset);
	}
}


/* Add Accept: headers for stuff we can convert for the phone */
static void add_accept_headers(List *headers) {
	int i;

	for (i = 0; i < NUM_CONVERTERS; i++) {
		if (http_type_accepted(headers, converters[i].result_type)
		    && !http_type_accepted(headers, converters[i].type)) {
			http_header_add(headers, "Accept", converters[i].type);
		}
	}
}


static void add_network_info(List *headers, WAPAddrTuple *addr_tuple) {
	if (octstr_len(addr_tuple->client->address) > 0) {
		http_header_add(headers, 
			"X_Network_Info", 
			octstr_get_cstr(addr_tuple->client->address));
	}
}


static void add_session_id(List *headers, long session_id) {
	if (session_id != -1) {
		char buf[40];
		sprintf(buf, "%ld", session_id);
		http_header_add(headers, "X-WAP-Session-ID", buf);
	}
}


static void fetch_thread(void *arg) {
	int status;
#ifdef POST_SUPPORT
	int ret=500;
#else
	int ret;
#endif
	WAPEvent *event;
	long client_SDU_size;
	Octstr *url, *os;
	List *session_headers;
	List *request_headers;
	List *actual_headers;
	List *resp_headers;
	WAPAddrTuple *addr_tuple;
	long session_id;
	struct content content;

#ifdef POST_SUPPORT
	
	int method;				/* This is the type of request, normally a get or a post */
	Octstr *request_body;	/* This is the request body. */

#endif	/* POST_SUPPORT */
	
	event = arg;
	if (event->type == S_MethodInvoke_Ind) {
		struct S_MethodInvoke_Ind *p;
		
		p = &event->u.S_MethodInvoke_Ind;
		session_headers = p->session_headers;
		request_headers = p->http_headers;
		url = octstr_duplicate(p->url);
		addr_tuple = p->addr_tuple;
		session_id = p->session_id;
		client_SDU_size = p->client_SDU_size;

#ifdef POST_SUPPORT

		request_body = octstr_duplicate(p->body);
		method = p->method;

#endif	/* POST_SUPPORT */

	} else {
		struct S_Unit_MethodInvoke_Ind *p;
		
		p = &event->u.S_Unit_MethodInvoke_Ind;
		session_headers = NULL;
		request_headers = p->request_headers;
		url = octstr_duplicate(p->request_uri);
		addr_tuple = p->addr_tuple;
		session_id = -1;
		client_SDU_size = 1024*1024; /* XXX */

#ifdef POST_SUPPORT

		request_body = octstr_duplicate(p->request_body);
		method = p->method;

#endif	/* POST_SUPPORT */

	}

	wsp_http_map_url(&url);

	actual_headers = list_create();

	if (session_headers != NULL)
		http_append_headers(actual_headers, session_headers);
	if (request_headers != NULL)
		http_append_headers(actual_headers, request_headers);

	add_accept_headers(actual_headers);
	add_charset_headers(actual_headers);
	add_network_info(actual_headers, addr_tuple);

#ifdef COOKIE_SUPPORT
	if (session_id != -1)
		if (set_cookies (actual_headers, find_session_machine_by_id(session_id)) == -1)
				error(0, "WSP: Failed to add cookies");
#endif
		
	add_kannel_version(actual_headers);
	add_session_id(actual_headers, session_id);

	http_header_pack(actual_headers);

#ifdef POST_SUPPORT

	switch (method) {

	case 0x40 :			/* Get request */

		ret = http_get_real(url, actual_headers, 
		     &content.url, &resp_headers, &content.body);
		break;

	case 0x60 :			/* Post request		*/

		ret = http_post_real(url, actual_headers, request_body,
		     &content.url, &resp_headers, &content.body);
		break;

	case 0x41 :			/* Options	*/
	case 0x42 :			/* Head		*/
	case 0x43 :			/* Delete	*/
	case 0x44 :			/* Trace	*/
	case 0x61 :			/* Put		*/

	default:
		error(0, "WSP: Method not supported: %d.", method);
		ret = 501;

	}

#else	/* POST_SUPPORT */

	ret = http_get_real(url, actual_headers, 
			     &content.url, &resp_headers, &content.body);

#endif	/* POST_SUPPORT */

	if (ret != HTTP_OK) {
		error(0, "WSP: http_get_real failed (%d), oops.", ret);
		status = 500; /* Internal server error; XXX should be 503 */
		content.type = octstr_create("text/plain");
		content.charset = octstr_create_empty();
	} else {
		http_header_get_content_type(resp_headers,
				&content.type, &content.charset);
		info(0, "WSP: Fetched <%s> (%s, charset='%s')", 
			octstr_get_cstr(url), octstr_get_cstr(content.type),
			octstr_get_cstr(content.charset));
		status = 200; /* OK */


#ifdef COOKIE_SUPPORT
		if (session_id != -1)
			if (get_cookies (resp_headers, find_session_machine_by_id(session_id)) == -1)
				error(0, "WSP: Failed to extract cookies");
#endif		
		
		if (convert_content(&content) < 0) {
			status = 500; /* XXX */
			warning(0, "WSP: All converters for `%s' failed.",
					octstr_get_cstr(content.type));
		}
	}

	gw_assert(actual_headers);
	while ((os = list_extract_first(actual_headers)) != NULL)
		octstr_destroy(os);
	list_destroy(actual_headers);

	if (resp_headers != NULL) {
		while ((os = list_extract_first(resp_headers)) != NULL)
			octstr_destroy(os);
		list_destroy(resp_headers);
	}
		
	if (octstr_len(content.body) > client_SDU_size) {
		status = 413; /* XXX requested entity too large */
		warning(0, "WSP: Entity at %s too large (size %lu B, limit %lu B)",
			octstr_get_cstr(url), octstr_len(content.body),
			client_SDU_size);
                octstr_destroy(content.body);
		content.body = octstr_create_empty();
		octstr_destroy(content.type);
		content.type = octstr_create("text/plain");
	}

	if (event->type == S_MethodInvoke_Ind) {
		WAPEvent *e = wap_event_create(S_MethodResult_Req);
		e->u.S_MethodResult_Req.server_transaction_id = 
			event->u.S_MethodInvoke_Ind.server_transaction_id;
		e->u.S_MethodResult_Req.status = status;
		e->u.S_MethodResult_Req.response_type = content.type;
		/* XXX Fill these in */
		e->u.S_MethodResult_Req.response_headers = NULL;
		e->u.S_MethodResult_Req.response_body = content.body;
		e->u.S_MethodResult_Req.session_id = session_id;
	
		wsp_session_dispatch_event(e);
	} else {
		WAPEvent *e = wap_event_create(S_Unit_MethodResult_Req);
		e->u.S_Unit_MethodResult_Req.addr_tuple = 
			wap_addr_tuple_duplicate(
				event->u.S_Unit_MethodInvoke_Ind.addr_tuple);
		e->u.S_Unit_MethodResult_Req.transaction_id = 
			event->u.S_Unit_MethodInvoke_Ind.transaction_id;
		e->u.S_Unit_MethodResult_Req.status = status;
		e->u.S_Unit_MethodResult_Req.response_type = content.type;
		/* XXX Fill these in */
		e->u.S_Unit_MethodResult_Req.response_headers = NULL;
		e->u.S_Unit_MethodResult_Req.response_body = content.body;
	
		wsp_unit_dispatch_event(e);
	}

	wap_event_destroy(event);
	octstr_destroy(content.url); /* body and type were re-used above */
	octstr_destroy(content.charset);
	octstr_destroy(url);

#ifdef POST_SUPPORT

	/* 
	 * Destroy the memory that was deep copied earlier. 
	 */
	octstr_destroy(request_body);

#endif	/* POST_SUPPORT */


}



/* Shut up WMLScript compiler status/trace messages. */
static void dev_null(const char *data, size_t len, void *context) {
	/* nothing */
}


static Octstr *convert_wml_to_wmlc(struct content *content) {
	Octstr *wmlc;
	int ret;

	ret = wml_compile(content->body, content->charset, &wmlc);
	if (ret == 0)
		return wmlc;
	warning(0, "WSP: WML compilation failed.");
	return NULL;
}


static Octstr *convert_wmlscript_to_wmlscriptc(struct content *content) {
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
				 octstr_get_cstr(content->url),
				 octstr_get_cstr(content->body),
				 octstr_len(content->body),
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


/* The interface for capability negotiation is a bit different from
 * the negotiation at WSP level, to make it easier to program.
 * The application layer gets a list of requested capabilities,
 * basically a straight decoding of the WSP level capabilities.
 * It replies with a list of all capabilities it wants to set or
 * refuse.  (Refuse by setting cap->data to NULL).  Any capabilities
 * it leaves out are considered "unknown; don't care".  The WSP layer
 * will either process those itself, or refuse them.
 *
 * At the WSP level, not sending a reply to a capability means accepting
 * what the client proposed.  If the application layer wants this to 
 * happen, it should set cap->data to NULL and cap->accept to 1.
 * (The WSP layer does not try to guess what kind of reply would be 
 * identical to what the client proposed, because the format of the
 * reply is often different from the format of the request, and this
 * is likely to be true for unknown capabilities too.)
 */
static List *negotiate_capabilities(List *req_caps) {
	/* Currently we don't know or care about any capabilities,
	 * though it is likely that "Extended Methods" will be
	 * the first. */
	return list_create();
}


/***********************************************************************
 * The following code implements the map-url mechanism
 */

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

/*
 * Add mapping for src URL to dst URL.
 */
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
