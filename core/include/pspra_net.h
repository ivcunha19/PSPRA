#ifndef PSPRA_NET_H
#define PSPRA_NET_H

#include "rc_client.h"

int pspra_net_init(void);
void pspra_net_shutdown(void);
void pspra_net_process_completed(void);

void pspra_server_call(const rc_api_request_t* request,
                       rc_client_server_callback_t callback,
                       void* callback_data,
                       rc_client_t* client);

#define pspRaServerCall pspra_server_call

#endif
