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
 * Private functions.
 */

static void main_thread(void *);
static void fetch_thread(void *);

static void  dev_null(const char *data, size_t len, void *context);
static int encode_content_type(const char *type);

static Octstr *convert_to_self(Octstr *stuff, Octstr *charset, char *url);
static Octstr *convert_wml_to_wmlc(Octstr *wml, Octstr *charset, char *url);
static Octstr *convert_wmlscript_to_wmlscriptc(Octstr *wmlscript, 
					       Octstr *charset, char *url);
static void wsp_http_map_url(Octstr **osp);


/***********************************************************************
 * The public interface to the application layer.
 */

void wap_appl_init(void) {
	gw_assert(run_status == limbo);
	queue = list_create();
	list_add_producer(queue);
	run_status = running;
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
			gwthread_create(fetch_thread, ind);

			res = wap_event_create(S_MethodInvoke_Res);
			res->u.S_MethodInvoke_Res.mid = 
				ind->u.S_MethodInvoke_Ind.mid;
			res->u.S_MethodInvoke_Res.tid = 
				ind->u.S_MethodInvoke_Ind.tid;
			res->u.S_MethodInvoke_Res.msmid = 
				ind->u.S_MethodInvoke_Ind.msmid;
			wsp_session_dispatch_event(res);

			break;
		case S_Unit_MethodInvoke_Ind:
			gwthread_create(fetch_thread, ind);
			break;
		default:
			panic(0, "APPL: Can't handle %s event",
				wap_event_name(ind->type));
			break;
		}
	}
}


static void add_kannel_version(List *headers) {
	Octstr *version;

	version = octstr_create("Kannel ");
	octstr_append_cstr(version, VERSION);
	http_header_add(headers, "X-WAP-Gateway", octstr_get_cstr(version));
	octstr_destroy(version);
}


static void fetch_thread(void *arg) {
	WAPEvent *e;
	int status;
	int ret;
	int i;
	int converter_failed;
	WAPEvent *event;
	unsigned long body_size, client_SDU_size;
	Octstr *url, *final_url, *resp_body, *body, *os, *type, *charset;
	List *session_headers;
	List *request_headers;
	List *actual_headers, *resp_headers;
	WAPAddrTuple *addr_tuple;
	long session_id;
	
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
	if (event->type == S_MethodInvoke_Ind) {
		struct S_MethodInvoke_Ind *p;
		
		p = &event->u.S_MethodInvoke_Ind;
		session_headers = p->session_headers;
		request_headers = p->http_headers;
		url = octstr_duplicate(p->url);
		addr_tuple = p->addr_tuple;
		session_id = p->session_id;
		client_SDU_size = p->client_SDU_size;
	} else {
		struct S_Unit_MethodInvoke_Ind *p;
		
		p = &event->u.S_Unit_MethodInvoke_Ind;
		session_headers = NULL;
		request_headers = p->request_headers;
		url = octstr_duplicate(p->request_uri);
		addr_tuple = p->addr_tuple;
		session_id = -1;
		client_SDU_size = 1024*1024; /* XXX */
	}

	wsp_http_map_url(&url);

	body = NULL;

	actual_headers = list_create();
	if (session_headers != NULL)
		http_append_headers(actual_headers, session_headers);
	if (request_headers)
		http_append_headers(actual_headers, request_headers);

	/* We can compile WML to WMLC */
	if (http_type_accepted(actual_headers, "application/vnd.wap.wmlc")
	    && !http_type_accepted(actual_headers, "text/vnd.wap.wml")) {
		http_header_add(actual_headers, "Accept", "text/vnd.wap.wml");
	}

	/* We can compile WMLScript */
	if (http_type_accepted(actual_headers,
		"application/vnd.wap.wmlscriptc")
	    && !http_type_accepted(actual_headers, "text/vnd.wap.wmlscript")) {
		http_header_add(actual_headers,
			"Accept", "text/vnd.wap.wmlscript");
	}

	if (octstr_len(addr_tuple->client->address) > 0) {
		http_header_add(actual_headers, 
			"X_Network_Info", 
			octstr_get_cstr(addr_tuple->client->address));
	}
	if (session_id != -1) {
		char buf[40];
		sprintf(buf, "%ld", session_id);
		http_header_add(actual_headers, "X-WAP-Session-ID", buf);
	}

	add_kannel_version(actual_headers);

	{
	    extern struct {
		char *charset;
		char *nro;
		unsigned char MIBenum;
		unsigned char *utf8map;
	    } character_sets[];
	    
	    for (i = 0; character_sets[i].charset != NULL; i++) {
	    
		char charsetname[16];

		strcpy (charsetname, character_sets[i].charset);
		strcat (charsetname, "-");
		strcat (charsetname, character_sets[i].nro);
		
		if (http_charset_accepted(actual_headers, charsetname)) {
	/*	    info(0, "WSP: charset %s already accepted.", charsetname); */
		} else {
	/*	    info(0, "WSP: charset %s added to accepted list.", charsetname); */
		    http_header_add(actual_headers, "Accept-Charset", charsetname);
		}

	    }
	}
	
	http_header_pack(actual_headers);

	ret = http_get_real(url, actual_headers, 
			     &final_url, &resp_headers, &resp_body);
	octstr_destroy(final_url);

	if (ret != HTTP_OK) {
		error(0, "WSP: http_get_real failed (%d), oops.", ret);
		status = 500; /* Internal server error; XXX should be 503 */
		type = octstr_create("text/plain");
	} else {
		http_header_get_content_type(resp_headers, &type, &charset);
		info(0, "WSP: Fetched <%s> (%s, charset='%s')", 
			octstr_get_cstr(url), octstr_get_cstr(type),
			octstr_get_cstr(charset));
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
	gw_assert(actual_headers);
	while ((os = list_extract_first(actual_headers)) != NULL)
		octstr_destroy(os);
	list_destroy(actual_headers);

	if (resp_headers != NULL) {
		while ((os = list_extract_first(resp_headers)) != NULL)
			octstr_destroy(os);
		list_destroy(resp_headers);
	}
		
	if (body == NULL)
		body_size = 0;
	else
		body_size = octstr_len(body);

	if (body != NULL && body_size > client_SDU_size) {
		status = 413; /* XXX requested entity too large */
		warning(0, "WSP: Entity at %s too large (size %lu B, limit %lu B)",
			octstr_get_cstr(url), body_size, client_SDU_size);
                octstr_destroy(body);
		body = NULL;
		octstr_destroy(type);
		type = octstr_create("text/plain");
	}

	if (event->type == S_MethodInvoke_Ind) {
		e = wap_event_create(S_MethodResult_Req);
		e->u.S_MethodResult_Req.server_transaction_id = 
			event->u.S_MethodInvoke_Ind.server_transaction_id;
		e->u.S_MethodResult_Req.status = status;
		e->u.S_MethodResult_Req.response_type = 
			encode_content_type(octstr_get_cstr(type));
		e->u.S_MethodResult_Req.response_body = body;
		e->u.S_MethodResult_Req.mid = event->u.S_MethodInvoke_Ind.mid;
		e->u.S_MethodResult_Req.tid = event->u.S_MethodInvoke_Ind.tid;
		e->u.S_MethodResult_Req.msmid = 
			event->u.S_MethodInvoke_Ind.msmid;
	
		wsp_session_dispatch_event(e);
	} else {
		e = wap_event_create(S_Unit_MethodResult_Req);
		e->u.S_Unit_MethodResult_Req.addr_tuple = 
			wap_addr_tuple_duplicate(
				event->u.S_Unit_MethodInvoke_Ind.addr_tuple);
		e->u.S_Unit_MethodResult_Req.tid = 
			event->u.S_Unit_MethodInvoke_Ind.tid;
		e->u.S_Unit_MethodResult_Req.status = status;
		e->u.S_Unit_MethodResult_Req.response_type = 
			encode_content_type(octstr_get_cstr(type));
		e->u.S_Unit_MethodResult_Req.response_body = body;
	
		wsp_unit_dispatch_event(e);
	}

	wap_event_destroy(event);
	octstr_destroy(type);
	octstr_destroy(url);
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

