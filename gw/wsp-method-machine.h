/*
 * wsp-method-machine.h - define a WSP method machine
 *
 * Lars Wirzenius
 */

#if 	!defined(INTEGER) || \
	!defined(ADDRTUPLE) || \
	!defined(MACHINE)
#error "Some required macro is missing."
#endif

MACHINE(
	INTEGER(id)
	INTEGER(state)
	ADDRTUPLE(addr_tuple)
	INTEGER(tid)
)


#undef INTEGER
#undef ADDRTUPLE
#undef MACHINE
