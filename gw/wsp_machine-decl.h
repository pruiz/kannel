/*
 * wsp_machine-decl.h
 *
 * Macro calls to generate WSP state machines. See the documentation for
 * guidance how to use and update these.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

SESSION_MACHINE({
	INTEGER(state);
	MUTEX(mutex);
	INTEGER(n_methods);
	INTEGER(session_id);
	METHOD_POINTER(method_machine);
	MUTEX(queue_lock);
	EVENT_POINTER(event_queue_head);
	EVENT_POINTER(event_queue_tail);
	OCTSTR(client_address);
	INTEGER(client_port);
	OCTSTR(server_address);
	INTEGER(server_port);
	SESSION_POINTER(next);

	INTEGER(set_caps);
	INTEGER(protocol_options);
	INTEGER(MOR_method);
	INTEGER(MOR_push);
	OCTSTR(aliases);
	OCTSTR(extended_methods);
	OCTSTR(header_code_pages);
	INTEGER(client_SDU_size);
	INTEGER(server_SDU_size);
})

METHOD_MACHINE({
	MUTEX(mutex);
	SESSION_POINTER(session_machine);
})


#undef MUTEX
#undef INTEGER
#undef OCTSTR
#undef EVENT_POINTER
#undef METHOD_POINTER
#undef SESSION_POINTER
#undef SESSION_MACHINE
#undef METHOD_MACHINE
