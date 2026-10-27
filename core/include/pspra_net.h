#ifndef PSPRA_NET_H
#define PSPRA_NET_H

#include "rc_client.h"

void pspRaServerCall(const rc_api_request_t* request,
                     rc_client_server_callback_t callback,
                     void* callback_data,
                     rc_client_t* client);

#endif