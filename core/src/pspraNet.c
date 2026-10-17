#include <stdio.h>
#include <stdlib.h> // Necessário para malloc, realloc e free
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "credentials.h"
#include "pspraNet.h"

void pspRaServerCall(const rc_api_request_t* request,
                     rc_client_server_callback_t callback,
                     void* callback_data,
                     rc_client_t* client) {
    (void)client;

    rc_api_server_response_t serverResponse;
    memset(&serverResponse, 0, sizeof(serverResponse));

    int networkSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (networkSocket < 0) {
        perror("[PSPRA Net] Error creating socket");
        serverResponse.http_status_code = 500;
        callback(&serverResponse, callback_data);
        return;
    }

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PSPRA_PROXY_PORT);

    if (inet_pton(AF_INET, PSPRA_PROXY_IP, &serverAddress.sin_addr) <= 0) {
        printf("[PSPRA Net] Endereço IP inválido: %s\n", PSPRA_PROXY_IP);
        close(networkSocket);
        serverResponse.http_status_code = 500;
        callback(&serverResponse, callback_data);
        return;
    }

    int connectionStatus = connect(networkSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (connectionStatus == -1) {
        perror("[PSPRA Net] Error connecting with Proxy");
        close(networkSocket);
        serverResponse.http_status_code = 500;
        callback(&serverResponse, callback_data);
        return; 
    }

    send(networkSocket, request->url, strlen(request->url), 0);
    send(networkSocket, "\n", 1, 0);

    if (request->post_data != NULL) {
        send(networkSocket, request->post_data, strlen(request->post_data), 0);
    }

    shutdown(networkSocket, SHUT_WR);

    size_t capacity = 4096;
    size_t totalReceived = 0;
    char* sResponse = (char*)malloc(capacity);

    ssize_t bytesReceived;
    while ((bytesReceived = recv(networkSocket, sResponse + totalReceived, capacity - totalReceived - 1, 0)) > 0) {
        totalReceived += bytesReceived;

        if (capacity - totalReceived < 1024) {
            capacity *= 2;
            sResponse = (char*)realloc(sResponse, capacity);
        }
    }

    if (totalReceived > 0) {
        sResponse[totalReceived] = '\0'; 
        serverResponse.body = sResponse;
        serverResponse.body_length = totalReceived;
        serverResponse.http_status_code = 200;
    } else {
        printf("[PSPRA Net] Nenhuma resposta recebida do Proxy.\n");
        serverResponse.http_status_code = 500;
    }

    close(networkSocket);
    callback(&serverResponse, callback_data);
    free(sResponse);
}