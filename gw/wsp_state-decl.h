/*
 * wsp_state-decl.h
 *
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

STATE_NAME(NULL_STATE)
STATE_NAME(CONNECTING)
STATE_NAME(CONNECTING_2)
STATE_NAME(CONNECTED)

ROW(NULL_STATE,
	TRInvokeIndication,
	e->tcl == 2 && wsp_deduce_pdu_type(e->user_data, 0) == Connect_PDU,
	{
		error(0, "State not yet implemented.");
	},
	CONNECTING)

#if 0

ROW(CONNECTING,
	SConnectResponse,
	1,
	{
		error(0, "State not yet implemented.");
	},
	CONNECTING_2)

ROW(CONNECTING_2,
	TRResultConfirmation,
	1, /* XXX check here that it is a Connect transaction! */
	{
		error(0, "State not yet implemented.");
	},
	CONNECTED)

#endif

#undef ROW
#undef STATE_NAME
