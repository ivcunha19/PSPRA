#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
#define PSPRA_LOG(...) pspDebugScreenPrintf(__VA_ARGS__)
#else
#include <pthread.h>
#include <semaphore.h>
#define PSPRA_LOG(...) printf(__VA_ARGS__)
#endif

#include "credentials.h"
#include "pspra_net.h"

#define PSPRA_NET_QUEUE_SIZE 8

#define SLOT_FREE         0
#define SLOT_PENDING_IO   1
#define SLOT_COMPLETED_IO 2

typedef struct {
    char url[512];
    char* post_data;
    rc_client_server_callback_t callback;
    void* callback_data;
    char* response_body;
    size_t response_len;
    int http_status_code;
    volatile int status;
} pspra_net_slot_t;

static pspra_net_slot_t g_queue[PSPRA_NET_QUEUE_SIZE];
static volatile int g_net_running = 0;
static int g_write_idx = 0;
static int g_worker_idx = 0;
static int g_dispatch_idx = 0;

#ifdef __PSP__
static SceUID g_queue_mutex = -1;
static SceUID g_work_sema = -1;
static SceUID g_worker_thid = -1;

static void lock_queue(void) {
    if (g_queue_mutex >= 0) {
        sceKernelWaitSema(g_queue_mutex, 1, NULL);
    }
}

static void unlock_queue(void) {
    if (g_queue_mutex >= 0) {
        sceKernelSignalSema(g_queue_mutex, 1);
    }
}

static void signal_work(void) {
    if (g_work_sema >= 0) {
        sceKernelSignalSema(g_work_sema, 1);
    }
}

static void wait_work(void) {
    if (g_work_sema >= 0) {
        sceKernelWaitSema(g_work_sema, 1, NULL);
    }
}
#else
static pthread_mutex_t g_queue_mutex;
static sem_t g_work_sema;
static pthread_t g_worker_thread;

static void lock_queue(void) {
    pthread_mutex_lock(&g_queue_mutex);
}

static void unlock_queue(void) {
    pthread_mutex_unlock(&g_queue_mutex);
}

static void signal_work(void) {
    sem_post(&g_work_sema);
}

static void wait_work(void) {
    sem_wait(&g_work_sema);
}
#endif

static int perform_proxy_http_request(const char* url, const char* post_data,
                                      char** out_body, size_t* out_len) {
    *out_body = NULL;
    *out_len = 0;

    int networkSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (networkSocket < 0) {
        perror("[PSPRA Net] Failed to create socket");
        return 500;
    }

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PSPRA_PROXY_PORT);

    if (inet_pton(AF_INET, PSPRA_PROXY_IP, &serverAddress.sin_addr) <= 0) {
        printf("[PSPRA Net] Invalid proxy IP address: %s\n", PSPRA_PROXY_IP);
        close(networkSocket);
        return 500;
    }

    if (connect(networkSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        perror("[PSPRA Net] Proxy connection failed");
        close(networkSocket);
        return 500;
    }

    // Protocol: "URL\nPOST_DATA"
    send(networkSocket, url, strlen(url), 0);
    send(networkSocket, "\n", 1, 0);

    if (post_data != NULL) {
        send(networkSocket, post_data, strlen(post_data), 0);
    }

    // Notify the proxy that we finished sending data
    shutdown(networkSocket, SHUT_WR);

    size_t capacity = 4096;
    size_t totalReceived = 0;
    char* sResponse = (char*)malloc(capacity);
    if (!sResponse) {
        close(networkSocket);
        return 500;
    }

    ssize_t bytesReceived;
    while ((bytesReceived = recv(networkSocket, sResponse + totalReceived, capacity - totalReceived - 1, 0)) > 0) {
        totalReceived += bytesReceived;

        if (capacity - totalReceived < 1024) {
            capacity *= 2;
            char* newBuf = (char*)realloc(sResponse, capacity);
            if (!newBuf) {
                free(sResponse);
                close(networkSocket);
                return 500;
            }
            sResponse = newBuf;
        }
    }

    close(networkSocket);

    if (totalReceived > 0) {
        sResponse[totalReceived] = '\0';
        *out_body = sResponse;
        *out_len = totalReceived;
        return 200;
    } else {
        printf("[PSPRA Net] Empty response received from proxy.\n");
        free(sResponse);
        return 500;
    }
}

static void process_next_pending_slot(void) {
    int slot_idx = -1;
    char url_copy[512];
    const char* post_data_ptr = NULL;

    lock_queue();
    for (int i = 0; i < PSPRA_NET_QUEUE_SIZE; i++) {
        int idx = (g_worker_idx + i) % PSPRA_NET_QUEUE_SIZE;
        if (g_queue[idx].status == SLOT_PENDING_IO) {
            slot_idx = idx;
            g_worker_idx = (idx + 1) % PSPRA_NET_QUEUE_SIZE;
            strncpy(url_copy, g_queue[idx].url, sizeof(url_copy) - 1);
            url_copy[sizeof(url_copy) - 1] = '\0';
            post_data_ptr = g_queue[idx].post_data;
            break;
        }
    }
    unlock_queue();

    if (slot_idx < 0) {
        return;
    }

    char* response_body = NULL;
    size_t response_len = 0;
    int status_code = perform_proxy_http_request(url_copy, post_data_ptr,
                                                 &response_body, &response_len);

    lock_queue();
    g_queue[slot_idx].response_body = response_body;
    g_queue[slot_idx].response_len = response_len;
    g_queue[slot_idx].http_status_code = status_code;
    g_queue[slot_idx].status = SLOT_COMPLETED_IO;
    unlock_queue();
}

#ifdef __PSP__
static int net_worker_thread(SceSize args, void* argp) {
    (void)args;
    (void)argp;

    while (g_net_running) {
        wait_work();
        if (!g_net_running) {
            break;
        }
        process_next_pending_slot();
    }

    sceKernelExitThread(0);
    return 0;
}
#else
static void* net_worker_thread(void* arg) {
    (void)arg;

    while (g_net_running) {
        wait_work();
        if (!g_net_running) {
            break;
        }
        process_next_pending_slot();
    }

    return NULL;
}
#endif

int pspra_net_init(void) {
    if (g_net_running) {
        return 1;
    }

    memset(g_queue, 0, sizeof(g_queue));
    g_write_idx = 0;
    g_worker_idx = 0;
    g_dispatch_idx = 0;
    g_net_running = 1;

#ifdef __PSP__
    g_queue_mutex = sceKernelCreateSema("PSPRA_NetMutex", 0, 1, 1, NULL);
    if (g_queue_mutex < 0) {
        PSPRA_LOG("[NetAsync] Failed to create queue mutex semaphore.\n");
        g_net_running = 0;
        return 0;
    }

    g_work_sema = sceKernelCreateSema("PSPRA_NetWork", 0, 0, PSPRA_NET_QUEUE_SIZE, NULL);
    if (g_work_sema < 0) {
        PSPRA_LOG("[NetAsync] Failed to create work semaphore.\n");
        sceKernelDeleteSema(g_queue_mutex);
        g_queue_mutex = -1;
        g_net_running = 0;
        return 0;
    }

    g_worker_thid = sceKernelCreateThread("PSPRA_NetWorker", net_worker_thread,
                                          0x28, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (g_worker_thid < 0) {
        PSPRA_LOG("[NetAsync] Failed to create network worker thread.\n");
        sceKernelDeleteSema(g_work_sema);
        sceKernelDeleteSema(g_queue_mutex);
        g_work_sema = -1;
        g_queue_mutex = -1;
        g_net_running = 0;
        return 0;
    }

    sceKernelStartThread(g_worker_thid, 0, NULL);
#else
    if (pthread_mutex_init(&g_queue_mutex, NULL) != 0) {
        PSPRA_LOG("[NetAsync] Failed to initialize queue mutex.\n");
        g_net_running = 0;
        return 0;
    }

    if (sem_init(&g_work_sema, 0, 0) != 0) {
        PSPRA_LOG("[NetAsync] Failed to initialize work semaphore.\n");
        pthread_mutex_destroy(&g_queue_mutex);
        g_net_running = 0;
        return 0;
    }

    if (pthread_create(&g_worker_thread, NULL, net_worker_thread, NULL) != 0) {
        PSPRA_LOG("[NetAsync] Failed to create network worker thread.\n");
        sem_destroy(&g_work_sema);
        pthread_mutex_destroy(&g_queue_mutex);
        g_net_running = 0;
        return 0;
    }
#endif

    PSPRA_LOG("[NetAsync] Background network worker initialized.\n");
    return 1;
}

void pspra_net_shutdown(void) {
    if (!g_net_running) {
        return;
    }

    g_net_running = 0;
    signal_work();

#ifdef __PSP__
    if (g_worker_thid >= 0) {
        sceKernelWaitThreadEnd(g_worker_thid, NULL);
        sceKernelDeleteThread(g_worker_thid);
        g_worker_thid = -1;
    }
    if (g_work_sema >= 0) {
        sceKernelDeleteSema(g_work_sema);
        g_work_sema = -1;
    }
    if (g_queue_mutex >= 0) {
        sceKernelDeleteSema(g_queue_mutex);
        g_queue_mutex = -1;
    }
#else
    pthread_join(g_worker_thread, NULL);
    sem_destroy(&g_work_sema);
    pthread_mutex_destroy(&g_queue_mutex);
#endif

    for (int i = 0; i < PSPRA_NET_QUEUE_SIZE; i++) {
        if (g_queue[i].post_data) {
            free(g_queue[i].post_data);
            g_queue[i].post_data = NULL;
        }
        if (g_queue[i].response_body) {
            free(g_queue[i].response_body);
            g_queue[i].response_body = NULL;
        }
        g_queue[i].status = SLOT_FREE;
    }

    PSPRA_LOG("[NetAsync] Background network worker shut down.\n");
}

void pspra_net_process_completed(void) {
    if (!g_net_running) {
        return;
    }

    int start_idx = g_dispatch_idx;
    for (int i = 0; i < PSPRA_NET_QUEUE_SIZE; i++) {
        int idx = (start_idx + i) % PSPRA_NET_QUEUE_SIZE;

        // Fast lock-free check to avoid 16 kernel semaphore syscalls per frame when idle
        if (g_queue[idx].status != SLOT_COMPLETED_IO) {
            continue;
        }

        rc_client_server_callback_t cb = NULL;
        void* cb_data = NULL;
        char* resp_body = NULL;
        size_t resp_len = 0;
        int status_code = 500;

        lock_queue();
        if (g_queue[idx].status == SLOT_COMPLETED_IO) {
            cb = g_queue[idx].callback;
            cb_data = g_queue[idx].callback_data;
            resp_body = g_queue[idx].response_body;
            resp_len = g_queue[idx].response_len;
            status_code = g_queue[idx].http_status_code;

            if (g_queue[idx].post_data) {
                free(g_queue[idx].post_data);
                g_queue[idx].post_data = NULL;
            }
            g_queue[idx].response_body = NULL;
            g_queue[idx].response_len = 0;
            g_queue[idx].callback = NULL;
            g_queue[idx].callback_data = NULL;
            g_queue[idx].status = SLOT_FREE;
            g_dispatch_idx = (idx + 1) % PSPRA_NET_QUEUE_SIZE;
        }
        unlock_queue();

        if (cb != NULL) {
            rc_api_server_response_t server_res;
            memset(&server_res, 0, sizeof(server_res));
            server_res.body = resp_body;
            server_res.body_length = resp_len;
            server_res.http_status_code = status_code;

            cb(&server_res, cb_data);

            if (resp_body) {
                free(resp_body);
            }
        }
    }
}

void pspra_server_call(const rc_api_request_t* request,
                       rc_client_server_callback_t callback,
                       void* callback_data,
                       rc_client_t* client) {
    (void)client;

    if (!g_net_running) {
        PSPRA_LOG("[NetAsync] Error: Network subsystem not initialized.\n");
        rc_api_server_response_t server_res;
        memset(&server_res, 0, sizeof(server_res));
        server_res.http_status_code = 500;
        callback(&server_res, callback_data);
        return;
    }

    int slot_idx = -1;

    lock_queue();
    for (int i = 0; i < PSPRA_NET_QUEUE_SIZE; i++) {
        int idx = (g_write_idx + i) % PSPRA_NET_QUEUE_SIZE;
        if (g_queue[idx].status == SLOT_FREE) {
            slot_idx = idx;
            g_write_idx = (idx + 1) % PSPRA_NET_QUEUE_SIZE;

            strncpy(g_queue[idx].url, request->url, sizeof(g_queue[idx].url) - 1);
            g_queue[idx].url[sizeof(g_queue[idx].url) - 1] = '\0';

            if (request->post_data != NULL) {
                size_t len = strlen(request->post_data) + 1;
                g_queue[idx].post_data = (char*)malloc(len);
                if (g_queue[idx].post_data) {
                    memcpy(g_queue[idx].post_data, request->post_data, len);
                }
            } else {
                g_queue[idx].post_data = NULL;
            }

            g_queue[idx].callback = callback;
            g_queue[idx].callback_data = callback_data;
            g_queue[idx].response_body = NULL;
            g_queue[idx].response_len = 0;
            g_queue[idx].http_status_code = 0;
            g_queue[idx].status = SLOT_PENDING_IO;
            break;
        }
    }
    unlock_queue();

    if (slot_idx < 0) {
        PSPRA_LOG("[NetAsync] Error: Request queue is full (8/8 slots busy).\n");
        rc_api_server_response_t server_res;
        memset(&server_res, 0, sizeof(server_res));
        server_res.http_status_code = 500;
        callback(&server_res, callback_data);
        return;
    }

    PSPRA_LOG("[NetAsync] Queued request (slot #%d)\n", slot_idx);
    signal_work();
}
