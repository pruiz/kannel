/*
 * wsp_http.c - The HTTP fetching and document processing thread
 *
 * Lars Wirzenius <liw@wapit.com>
 * Capabilities/headers by Kalle Marjola <rpr@wapit.com>
 */

#include <assert.h>
#include <string.h>

#include "gwlib.h"
#include "wsp.h"
#include "ws.h"
#include "wml_compiler.h"


static void  dev_null(const char *data, size_t len, void *context);
static int encode_content_type(const char *type);

static Octstr *convert_to_self(Octstr *stuff, char *url);
static Octstr *convert_wml_to_wmlc_old(Octstr *wml, char *url);
static Octstr *convert_wml_to_wmlc_new(Octstr *wml, char *url);
static Octstr *convert_wmlscript_to_wmlscriptc(Octstr *wmlscript, char *url);


void *wsp_http_thread(void *arg) {
	char *type, *data;
	size_t size;
	Octstr *body, *input;
	WSPEvent *e;
	int status;
	int ret;
	int i;
	int converter_failed;
	char *url;
	WSPEvent *event;
	unsigned long body_size, client_SDU_size;
	WTPMachine *wtp_sm;
	WSPMachine *sm;
	HTTPHeader *headers, *last, *h, *new_h;
	int wml_ok, wmlc_ok, wmlscript_ok, wmlscriptc_ok;
	
	static struct {
		char *type;
		char *result_type;
		Octstr *(*convert)(Octstr *, char *);
	} converters[] = {
		{ "text/vnd.wap.wml",
		  "application/vnd.wap.wmlc",
		  convert_wml_to_wmlc_new },
		{ "text/vnd.wap.wml",
		  "application/vnd.wap.wmlc",
		  convert_wml_to_wmlc_old },
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

	debug(0, "WSP: wsp_http_thread starts");

	event = arg;
	wtp_sm = event->SMethodInvokeResult.machine;
	sm = event->SMethodInvokeResult.session;
	debug(0, "WSP: Sending S-MethodInvoke.Res to WSP");
	wsp_dispatch_event(wtp_sm, event);

	url = octstr_get_cstr(event->SMethodInvokeResult.url);
	debug(0, "WSP: url is <%s>", url);

	body = NULL;

	headers = NULL;
	last = NULL;
	for (h = sm->http_headers; h != NULL; h = h->next) {
		new_h = header_create(h->key, h->value);
		if (last != NULL)
			last->next = new_h;
		else
			headers = new_h;
		last = new_h;
	}
	for (h = event->SMethodInvokeResult.http_headers; h != NULL; h = h->next) {
		new_h = header_create(h->key, h->value);
		if (last != NULL)
			last->next = new_h;
		else
			headers = new_h;
		last = new_h;
	}
	header_pack(headers);
	wml_ok = 0;
	wmlc_ok = 0;
	wmlscript_ok = 0;
	wmlscriptc_ok = 0;
	for (h = headers; h != NULL; h = h->next) {
		if (strcasecmp(h->key, "Accept") == 0) {
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
		new_h = header_create("Accept", "text/vnd.wap.wml");
		new_h->next = headers;
		headers = new_h;
	}
	if (wmlscriptc_ok && !wmlscript_ok) {
		new_h = header_create("Accept", "text/vnd.wap.wmlscript");
		new_h->next = headers;
		headers = new_h;
	}
	header_pack(headers);

	ret = http_get_u(url, &type, &data, &size, headers);
	if (ret == -1) {
		error(0, "WSP: http_get failed, oops.");
		status = 500; /* Internal server error; XXX should be 503 */
		type = "text/plain";
	} else {
		info(0, "WSP: Fetched <%s>", url);
		debug(0, "WSP: Type is <%s> (0x%02x)", type,
			encode_content_type(type));
		status = 200; /* OK */
		
		input = octstr_create_from_data(data, size);

		converter_failed = 0;
		for (i = 0; i < num_converters; ++i) {
			if (strcmp(type, converters[i].type) == 0) {
				debug(0, "WSP: converting to `%s'",
					converters[i].result_type);
				body = converters[i].convert(input, url);
				if (body != NULL)
					break;
				converter_failed = 1;
			}
		}

		if (i < num_converters)
			type = converters[i].result_type;
		else if (converter_failed) {
			status = 500; /* XXX */
			warning(0, "WSP: All converters for `%s' failed.",
					type);
		} else {
			status = 415; /* Unsupported media type */
			warning(0, "WSP: Unsupported content type `%s'", type);
			debug(0, "Content of unsupported content:");
			octstr_dump(input);
		}
	}
		
	if (body == NULL)
		body_size = 0;
	else
		body_size = octstr_len(body);
	client_SDU_size = event->SMethodInvokeResult.session->client_SDU_size;

	if (body != NULL && body_size > client_SDU_size) {
		status = 413; /* XXX requested entity too large */
		warning(0, "WSP: Entity at %s too large (size %lu B, limit %lu B)",
			url, body_size, client_SDU_size);
		body = NULL;
		type = "text/plain";
	}

	e = wsp_event_create(SMethodResultRequest);
	e->SMethodResultRequest.server_transaction_id = 
		event->SMethodInvokeResult.server_transaction_id;
	e->SMethodResultRequest.status = status;
	e->SMethodResultRequest.response_type = encode_content_type(type);
	e->SMethodResultRequest.response_body = body;
	e->SMethodResultRequest.machine = event->SMethodInvokeResult.machine;

	debug(0, "WSP: sending S-MethodResult.req to WSP");
	wsp_dispatch_event(event->SMethodInvokeResult.machine, e);

	debug(0, "WSP: wsp_http_thread ends");
	return NULL;
}



/* Shut up WMLScript compiler status/trace messages. */
static void  dev_null(const char *data, size_t len, void *context) {
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
	int i;
	
	for (i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i)
		if (strcmp(type, tab[i].type) == 0)
			return tab[i].shortint;
	error(0, "WSP: Unknown content type <%s>, assuming text/plain.", type);
	return 0x03;
}


static Octstr *convert_to_self(Octstr *stuff, char *url) {
	return stuff;
}


static Octstr *convert_wml_to_wmlc_old(Octstr *wml, char *url) {
	char name[10*1024];
	char cmd[20*1024];
	char *test_wml;
	FILE *f;
	char data[100*1024];
	size_t n;
	Octstr *wmlc;
	
	debug(0, "WSP: Compiling WML");

	tmpnam(name);
	f = fopen(name, "w");
	if (f == NULL)
		goto error;
	if (fwrite(octstr_get_cstr(wml), 1, octstr_len(wml), f) == -1)
		goto error;
	fclose(f);
	
	test_wml = getenv("TEST_WML");
	if (test_wml == NULL)
		test_wml = "./test_wml";
	sprintf(cmd, "%s %s", test_wml, name);
	debug(0, "WSP: WML cmd: <%s>", cmd);
	f = popen(cmd, "r");
	if (f == NULL)
		goto error;
	n = fread(data, 1, sizeof(data), f);
	debug(0, "WSP: Read %lu bytes of compiled WMLC", (unsigned long) n);
	fclose(f);
	wmlc = octstr_create_from_data(data, n);

	debug(0, "WML: Compiled WML:");
	octstr_dump(wmlc);

	debug(0, "WSP: WML compilation done.");
	return wmlc;

error:
	panic(errno, "Couldn't write temp file for WML compilation.");
	return NULL;
}


static Octstr *convert_wml_to_wmlc_new(Octstr *wml, char *url) {
#if 0
	Octstr *wmlc, *wmlscripts;
	int ret;
	
	ret = wml_compile(wml, &wmlc, &wmlscripts);
	octstr_destroy(wmlscripts);
	if (ret == 0)
		return wmlc;
	return NULL;
#else
	return NULL;
#endif
}


static Octstr *convert_wmlscript_to_wmlscriptc(Octstr *wmlscript, char *url) {
	WsCompilerParams params;
	WsCompilerPtr compiler;
	WsResult result;
	unsigned char *result_data;
	size_t result_size;
	Octstr *wmlscriptc;

	debug(0, "WSP: Compiling WMLScript");

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

	debug(0, "WSP: WMLScript compilation done.");
	return wmlscriptc;
}
