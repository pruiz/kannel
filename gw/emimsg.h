/*
 * emimsg.h
 *
 * Declarations needed with EMI messages
 * Uoti Urpala 2001 */

#ifndef EMIMSG_H
#define EMIMSG_H


#include "gwlib/gwlib.h"


struct emimsg {
    int trn;
    char or;
    int ot;
    int num_fields;
    Octstr **fields;
};


/* All the 50-series messages have the same number of fields */
enum {
    E50_ADC, E50_OADC, E50_AC, E50_NRQ, E50_NADC, E50_NT, E50_NPID, E50_LRQ,
    E50_LRAD, E50_LPID, E50_DD, E50_DDT, E50_VP, E50_RPID, E50_SCTS, E50_DST,
    E50_RSN, E50_DSCTS, E50_MT, E50_NB, E50_NMSG=20, E50_AMSG=20, E50_TMSG=20,
    E50_MMS, E50_PR, E50_DCS, E50_MCLS, E50_RPI, E50_CPG, E50_RPLY, E50_OTOA,
    E50_HPLMN, E50_XSER, E50_RES4, E50_RES5, SZ50
};


enum {
    E60_OADC, E60_OTON, E60_ONPI, E60_STYP, E60_PWD, E60_NPWD, E60_VERS,
    E60_LADC, E60_LTON, E60_LNPI, E60_OPID, E60_RES1, SZ60
};


struct emimsg *emimsg_create_op(int ot, int trn);


struct emimsg *emimsg_create_reply(int ot, int trn, int positive);


void emimsg_destroy(struct emimsg *emimsg);


/* Create an emimsg struct from the string. */
/* Doesn't check that the string is strictly according to format */
struct emimsg *get_fields(Octstr *message);


/* Send emimsg over conn using the EMI protocol. */
int emimsg_send(Connection *conn, struct emimsg *emimsg);

#endif
