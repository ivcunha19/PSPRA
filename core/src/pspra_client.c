#include <stdio.h>
#include <string.h>
#include "md5.h"
#include "rc_client.h"
#include "pspra_net.h"
#include "pspra_client.h"

// Platform-agnostic logging and delay macros (Terminal on Linux, LCD Screen on PSP)
#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
#define PSPRA_LOG(...) pspDebugScreenPrintf(__VA_ARGS__)
#define PSPRA_DELAY_US(us) sceKernelDelayThread(us)
#else
#include <unistd.h>
#define PSPRA_LOG(...) printf(__VA_ARGS__)
#define PSPRA_DELAY_US(us) usleep(us)
#endif

// Set to 1 during fake RAM tests (blocks server unlocks).
// Set to 0 when playing real games to save Softcore unlocks to your RA profile!
#ifndef PSPRA_SPECTATOR_MODE
#define PSPRA_SPECTATOR_MODE 0
#endif

static rc_client_t* g_client = NULL;
static uint8_t*     g_emulator_ram = NULL;
static uint32_t     g_emulator_ram_size = 0;

static volatile int g_login_complete = 0;
static volatile int g_load_game_complete = 0;

static char g_notification_text[128] = {0};
static int  g_notification_frames = 0;

// ============================================================================
// 0. INTERNAL RCHEEVOS LOGGER CALLBACK
// ============================================================================
static void pspra_log_message(const char* message, const rc_client_t* client) {
    (void)client;
    PSPRA_LOG("[rcheevos] %s\n", message);
}

// ============================================================================
// 1. MEMORY READ CALLBACK
// ============================================================================
static uint32_t pspra_read_memory(uint32_t address, uint8_t* buffer, 
                                  uint32_t num_bytes, rc_client_t* client) {
    (void)client;

    if (g_emulator_ram == NULL || (address + num_bytes) > g_emulator_ram_size) {
        return 0;
    }

    const uint8_t* src = &g_emulator_ram[address];
    switch (num_bytes) {
        case 1:
            buffer[0] = src[0];
            return 1;
        case 2:
            buffer[0] = src[0];
            buffer[1] = src[1];
            return 2;
        case 4:
            buffer[0] = src[0];
            buffer[1] = src[1];
            buffer[2] = src[2];
            buffer[3] = src[3];
            return 4;
        default:
            memcpy(buffer, src, num_bytes);
            return num_bytes;
    }
}

// ============================================================================
// 2. EVENT HANDLER CALLBACK
// ============================================================================
static void pspra_event_handler(const rc_client_event_t* event, rc_client_t* client) {
    (void)client;

    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            snprintf(g_notification_text, sizeof(g_notification_text),
                     "ACHIEVEMENT UNLOCKED: %s (%u pts)",
                     event->achievement->title,
                     event->achievement->points);
            g_notification_frames = 240;

            PSPRA_LOG("\n**************************************************\n");
            PSPRA_LOG("[PSPRA] ACHIEVEMENT UNLOCKED: %s\n", event->achievement->title);
            PSPRA_LOG("[PSPRA] Description: %s (%u pts)\n", 
                      event->achievement->description, 
                      event->achievement->points);
            PSPRA_LOG("**************************************************\n\n");
            break;

        case RC_CLIENT_EVENT_GAME_COMPLETED:
            PSPRA_LOG("[PSPRA] MASTERED! All achievements unlocked!\n");
            break;

        default:
            break;
    }
}

// ============================================================================
// 3. LOGIN CALLBACK
// ============================================================================
static void pspra_login_callback(int result, const char* error_message, 
                                 rc_client_t* client, void* userdata) {
    (void)userdata;

    if (result != RC_OK) {
        PSPRA_LOG("[PSPRA] Login failed: %s\n", error_message);
        g_login_complete = 1;
        return;
    }

    const rc_client_user_t* user = rc_client_get_user_info(client);
    if (user) {
        PSPRA_LOG("[PSPRA] Logged in as %s (Softcore Score: %u pts)\n",
                  user->display_name,
                  user->score_casual);
    }

    g_login_complete = 1;
}

// ============================================================================
// 4. LOAD GAME CALLBACK
// ============================================================================
static void pspra_load_game_callback(int result, const char* error_message, 
                                     rc_client_t* client, void* userdata) {
    (void)userdata;

    if (result != RC_OK) {
        PSPRA_LOG("[PSPRA] Failed to load game: %s\n", error_message);
        g_load_game_complete = 1;
        return;
    }

#if PSPRA_SPECTATOR_MODE
    // Enabled AFTER startsession so triggers are active locally, but not sent to server
    rc_client_set_spectator_mode_enabled(client, 1);
    PSPRA_LOG("[PSPRA] Safety: Spectator Mode ENABLED (local triggers only).\n");
#endif

    const rc_client_game_t* game = rc_client_get_game_info(client);
    if (game) {
        rc_client_user_game_summary_t summary;
        rc_client_get_user_game_summary(client, &summary);

        PSPRA_LOG("[PSPRA] Game loaded: %s (ID: %u)\n", game->title, game->id);
        PSPRA_LOG("[PSPRA] Progress: %u/%u achievements (%u/%u pts)\n",
                  summary.num_unlocked_achievements,
                  summary.num_promoted_achievements,
                  summary.points_unlocked,
                  summary.points_available);
    }

    g_load_game_complete = 1;
}

// ============================================================================
// 5. PUBLIC API FUNCTIONS
// ============================================================================
int pspra_init(uint8_t* ram_pointer, uint32_t ram_size) {
    g_emulator_ram = ram_pointer;
    g_emulator_ram_size = ram_size;

    if (!pspra_net_init()) {
        PSPRA_LOG("[PSPRA] Error: Failed to initialize async network subsystem.\n");
        return 0;
    }

    g_client = rc_client_create(pspra_read_memory, pspra_server_call);
    if (!g_client) {
        PSPRA_LOG("[PSPRA] Error: Failed to initialize rc_client.\n");
        pspra_net_shutdown();
        return 0;
    }

    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_INFO, pspra_log_message);
    rc_client_set_event_handler(g_client, pspra_event_handler);

    // Strictly enforce Softcore mode for RetroAchievements compliance
    rc_client_set_hardcore_enabled(g_client, 0);

    // Enable Encore Mode so achievements can be triggered repeatedly during hardware testing
    rc_client_set_encore_mode_enabled(g_client, 1);

    return 1;
}

void pspra_login(const char* username, const char* password) {
    if (!g_client) return;

    g_login_complete = 0;
    PSPRA_LOG("[PSPRA] Logging in as '%s'...\n", username);
    rc_client_begin_login_with_password(g_client, username, password, pspra_login_callback, NULL);

    while (!g_login_complete) {
        pspra_net_process_completed();
        PSPRA_DELAY_US(10000);
    }
}

void pspra_load_game(const char* rom_hash) {
    if (!g_client) return;

    g_load_game_complete = 0;
    PSPRA_LOG("[PSPRA] Loading game data for hash '%s'...\n", rom_hash);
    rc_client_begin_load_game(g_client, rom_hash, pspra_load_game_callback, NULL);

    while (!g_load_game_complete) {
        pspra_net_process_completed();
        PSPRA_DELAY_US(10000);
    }
}

void pspra_update_frame(void) {
    if (g_notification_frames > 0) {
        g_notification_frames--;
    }

    if (g_client) {
        pspra_net_process_completed();
        rc_client_do_frame(g_client);
    }
}

void pspra_destroy(void) {
    if (g_client) {
        rc_client_destroy(g_client);
        g_client = NULL;
    }

    pspra_net_shutdown();
}

void pspra_compute_snes_md5(const uint8_t* rom_buf, size_t rom_size, char out_hex[33]) {
    if (!rom_buf || !out_hex) {
        if (out_hex) {
            out_hex[0] = '\0';
        }
        return;
    }

    if ((rom_size % 1024) == 512 && rom_size > 512) {
        rom_buf += 512;
        rom_size -= 512;
    }

    md5_state_t state;
    md5_byte_t digest[16];
    md5_init(&state);
    md5_append(&state, (const md5_byte_t*)rom_buf, (int)rom_size);
    md5_finish(&state, digest);

    for (int i = 0; i < 16; i++) {
        snprintf(&out_hex[i * 2], 3, "%02x", digest[i]);
    }
    out_hex[32] = '\0';
}

const char* pspra_get_active_notification(void) {
    if (g_notification_frames > 0 && g_notification_text[0] != '\0') {
        return g_notification_text;
    }
    return NULL;
}
