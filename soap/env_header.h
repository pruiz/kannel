/******************************************************************************\
 *                                                                            *
 * Import                                                                     *
 *                                                                            *
\******************************************************************************/

#import "wsse.h"    // wsse = <http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd>

/******************************************************************************\
 *                                                                            *
 * SOAP Header                                                                *
 *                                                                            *
\******************************************************************************/

/**
The SOAP Header is part of the gSOAP context and its content is accessed
through the soap.header variable. You may have to set the soap.actor variable
to serialize SOAP Headers with SOAP-ENV:actor or SOAP-ENV:role attributes.
*/

struct SOAP_ENV__Header
{
    mustUnderstand                      // must be understood by receiver
    _wsse__Security                     *wsse__Security;    ///< TODO: Check element type (imported type)
};
