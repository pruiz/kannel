/*
 * wsp-session-machine.h - Define a WSP session machine.
 *
 * Lars Wirzenius
 */


#if !defined(HTTPHEADERS) || \
	!defined(INTEGER) || \
	!defined(OCTSTR) || \
	!defined(ADDRTUPLE) || \
	!defined(COOKIES) || \
	!defined(METHODMACHINES) || \
	!defined(MACHINE)
#error "Some required macro is missing."
#endif


MACHINE(
	INTEGER(state)
	INTEGER(connect_handle)
	INTEGER(session_id)
	ADDRTUPLE(addr_tuple)

	INTEGER(set_caps)
	INTEGER(protocol_options)
	INTEGER(MOR_method)
	INTEGER(MOR_push)
	OCTSTR(aliases)
	OCTSTR(extended_methods)
	OCTSTR(header_code_pages)
	INTEGER(client_SDU_size)
	INTEGER(server_SDU_size)
	
	HTTPHEADERS(http_headers)
	COOKIES(cookies)
	METHODMACHINES(methodmachines)
)

#undef INTEGER
#undef OCTSTR
#undef HTTPHEADERS
#undef ADDRTUPLE
#undef METHODMACHINES
#undef MACHINE
#undef COOKIES
