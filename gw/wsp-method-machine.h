/*
 * wsp-method-machine.h - define a WSP method machine
 *
 * Lars Wirzenius
 */

#if 	!defined(INTEGER) || \
	!defined(ADDRTUPLE) || \
	!defined(EVENT) || \
	!defined(MACHINE)
#error "Some required macro is missing."
#endif

MACHINE(
	INTEGER(transaction_id)
	INTEGER(state)
	ADDRTUPLE(addr_tuple)
	EVENT(invoke)
	INTEGER(session_id)
)


#undef INTEGER
#undef ADDRTUPLE
#undef EVENT
#undef MACHINE
