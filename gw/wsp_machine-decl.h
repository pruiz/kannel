/*
 * wsp_machine-decl.h
 *
 * Macro calls to generate WSP state machines. See the documentation for
 * guidance how to use and update these.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

SESSION_MACHINE({
	INTEGER(n_methods);
	METHOD_POINTER(method_machine);
})

METHOD_MACHINE({
	SESSION_POINTER(session_machine);
})


#undef ROW
#undef STATE_NAME
