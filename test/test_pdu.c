/*
 * test_pdu.c - test gw/wtp_pdu packing and unpacking.
 *
 * Richard Braakman <dark@wapit.com>
 */
 
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gw/wtp_pdu.h"
#include "gw/wsp_pdu.h"

int main(int argc, char **argv) {
	int i;
	Octstr *packet = NULL;
	Octstr *newpacket = NULL;
	WTP_PDU *pdu = NULL;
	Octstr *wsp_data = NULL;
	WSP_PDU *wsp = NULL;

	gwlib_init();

	for (i = 1; i < argc; i++) {
		octstr_destroy(packet);
		octstr_destroy(newpacket);
		octstr_destroy(wsp_data);
		wtp_pdu_destroy(pdu);
		wsp_pdu_destroy(wsp);

		packet = octstr_read_file(argv[i]);
		pdu = wtp_pdu_unpack(packet);
		if (!pdu) {
			warning(0, "Unpacking PDU %s failed", argv[i]);
			continue;
		}
		debug("test", 0, "PDU %s:", argv[i]);  
		wtp_pdu_dump(pdu, 0);
		newpacket = wtp_pdu_pack(pdu);
		if (!newpacket) {
			warning(0, "Repacking PDU %s failed", argv[i]);
			continue;
		}
		if (octstr_compare(packet, newpacket) != 0) {
			error(0, "Repacking PDU %s changed it", argv[i]);
			debug("test", 0, "Original:");
			octstr_dump(packet, 1);
			debug("test", 0, "New:");
			octstr_dump(newpacket, 1);
			continue;
		}
		if (pdu->type == Invoke) {
			wsp_data = pdu->u.Invoke.user_data;
		} else if (pdu->type == Result) {
			wsp_data = pdu->u.Result.user_data;
		} else {
			continue;
		}
		wsp_data = octstr_duplicate(wsp_data);

		wsp = wsp_pdu_unpack(wsp_data);
		if (!wsp) {
			warning(0, "Unpacking WSP data in %s failed", argv[i]);
			continue;
		}
		wsp_pdu_dump(wsp, 0);
		octstr_destroy(newpacket);
		newpacket = wsp_pdu_pack(wsp);
		if (!newpacket) {
			warning(0, "Repacking WSP data in %s failed", argv[i]);
			continue;
		}
		if (octstr_compare(wsp_data, newpacket) != 0) {
			error(0, "Repacking WSP data in %s changed it",
				argv[i]);
			debug("test", 0, "Original:");
			octstr_dump(wsp_data, 1);
			debug("test", 0, "New:");
			octstr_dump(newpacket, 1);
			continue;
		}
	}

	octstr_destroy(packet);
	octstr_destroy(newpacket);
	wtp_pdu_destroy(pdu);

	gwlib_shutdown();
	exit(0);
}
