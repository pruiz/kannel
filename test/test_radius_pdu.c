/*
 * test_radius_pdu.c - test RADIUS PDU packing and unpacking.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "radius/radius_pdu.h"

int main(int argc, char **argv)
{
    Octstr *data, *filename, *rdata;
    RADIUS_PDU *pdu, *r;

    gwlib_init();
    
    get_and_set_debugs(argc, argv, NULL);

    filename = octstr_create(argv[1]);
    data = octstr_read_file(octstr_get_cstr(filename));

    debug("",0,"Calling radius_pdu_unpack() now");
    pdu = radius_pdu_unpack(data);

    debug("",0,"PDU type code: %ld", pdu->u.Accounting_Request.code);
    debug("",0,"PDU identifier: %ld", pdu->u.Accounting_Request.identifier);
    debug("",0,"PDU length: %ld", pdu->u.Accounting_Request.length);
    octstr_dump_short(pdu->u.Accounting_Request.authenticator,0, "PDU authenticator");

    /* XXX authenticator md5 check does not work?! */
    /* radius_authenticate_pdu(pdu, data, octstr_imm("radius")); */

    /* create response PDU */
    r = radius_pdu_create(0x05, pdu);

    /* create response authenticator 
     * code+identifier(req)+length+authenticator(req)+(attributes)+secret 
     */
    r->u.Accounting_Response.identifier = pdu->u.Accounting_Request.identifier;
    r->u.Accounting_Response.authenticator = octstr_duplicate(pdu->u.Accounting_Request.authenticator);

    rdata = radius_pdu_pack(r);

    /* creates response autenticator in encoded PDU */
    radius_authenticate_pdu(r, &rdata, octstr_imm("radius"));

    octstr_dump_short(rdata, 0, "Encoded Response PDU");

    debug("",0,"Destroying RADIUS_PDUs");
    radius_pdu_destroy(pdu);
    radius_pdu_destroy(r);
    
    octstr_destroy(data);
    octstr_destroy(rdata);
    octstr_destroy(filename);

    gwlib_shutdown();

    return 0;
}
