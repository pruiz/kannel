/*
 * wap-error.h - interface to error handling layer
 */

#ifndef WAP_ERROR_H
#define WAP_ERROR_H

#include "wap/wap.h"

Octstr* error_requesting_back(Octstr *url, Octstr *referer);
Octstr* error_requesting(Octstr *url);

#endif
