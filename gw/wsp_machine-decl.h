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
	METHOD_POINTER(method_machine);
	MUTEX(queue_lock);
	EVENT_POINTER(event_queue_head);
	EVENT_POINTER(event_queue_tail);
})

METHOD_MACHINE({
	MUTEX(mutex);
	SESSION_POINTER(session_machine);
})


#undef MUTEX
#undef INTEGER
#undef EVENT_POINTER
#undef METHOD_POINTER
#undef SESSION_POINTER
#undef SESSION_MACHINE
#undef METHOD_MACHINE
