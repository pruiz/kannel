/*
 * wsp-session-machine.h - Define a WSP session machine.
 *
 * Lars Wirzenius
 */


#if !defined(HTTPHEADERS) || \
	!defined(INTEGER) || \
	!defined(OCTSTR) || \
	!defined(ADDRTUPLE) || \
	!defined(MACHINE)
#error "Some required macro is missing."
#endif


MACHINE(
	INTEGER(state)
	INTEGER(connect_tid)
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
)

#undef INTEGER
#undef OCTSTR
#undef HTTPHEADERS
#undef ADDRTUPLE
#undef MACHINE
