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
	!defined(CAPABILITIES) || \
	!defined(MACHINE)
#error "Some required macro is missing."
#endif


MACHINE(
	INTEGER(state)
	INTEGER(connect_handle)
	INTEGER(session_id)
	ADDRTUPLE(addr_tuple)

	CAPABILITIES(request_caps)
	CAPABILITIES(reply_caps)

	INTEGER(MOR_push)
	INTEGER(client_SDU_size)
	
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
#undef CAPABILITIES
