/*
 * wsp_machine-decl.h
 *
 * Macro calls to generate WSP state machines. See the documentation for
 * guidance how to use and update these.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#if !defined(HTTPHEADER) || !defined(INTEGER) || \
	!defined(OCTSTR) || !defined(EVENT_POINTER) || \
	!defined(METHOD_POINTER) || !defined(SESSION_POINTER) || \
	!defined(SESSION_MACHINE) || !defined(METHOD_MACHINE) || \
	!defined(LIST)
#error "wsp_machine-decl.h: Some required macro is missing."
#endif


SESSION_MACHINE(
	INTEGER(unused)
	INTEGER(state)
	INTEGER(n_methods)
	INTEGER(session_id)
	METHOD_POINTER(method_machine)
	OCTSTR(client_address)
	INTEGER(client_port)
	OCTSTR(server_address)
	INTEGER(server_port)

	INTEGER(set_caps)
	INTEGER(protocol_options)
	INTEGER(MOR_method)
	INTEGER(MOR_push)
	OCTSTR(aliases)
	OCTSTR(extended_methods)
	OCTSTR(header_code_pages)
	INTEGER(client_SDU_size)
	INTEGER(server_SDU_size)
	
	HTTPHEADER(http_headers)
)

METHOD_MACHINE(
	SESSION_POINTER(session_machine)
)


#undef INTEGER
#undef OCTSTR
#undef EVENT_POINTER
#undef METHOD_POINTER
#undef SESSION_POINTER
#undef SESSION_MACHINE
#undef METHOD_MACHINE
#undef HTTPHEADER
#undef LIST
