#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psputility.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "credentials.h"
#include "pspra_client.h"

PSP_MODULE_INFO("PSPRA_Sample", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(4096);

#define DUMMY_RAM_SIZE 131072
#define TEST_ROM_HASH "cdd3c8c37322978ca8669b34bc89c804"

static int g_running = 1;

// ============================================================================
// 1. PSP EXIT CALLBACK (HOME BUTTON)
// ============================================================================
int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    g_running = 0;
    return 0;
}

int CallbackThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, 0);
    }
    return thid;
}

// ============================================================================
// 2. PSP WI-FI INITIALIZATION, PROFILE SELECTOR & AP CONNECTION
// ============================================================================
#define MAX_WIFI_PROFILES 20

typedef struct {
    int id;
    char name[128];
    char ssid[128];
} WifiProfile;

static int g_apctl_handler_id = -1;

static void apctl_event_handler(int oldState, int newState, int event, int error, void *pArg) {
    (void)pArg;
    if (error != 0) {
        pspDebugScreenPrintf("[Net] Apctl Error: 0x%08X (state %d -> %d, event %d)\n",
                             (unsigned int)error, oldState, newState, event);
    }
}

static int psp_wifi_init(void) {
    int err;

    pspDebugScreenPrintf("[Net] Loading PSP network modules...\n");
    err = sceUtilityLoadModule(PSP_MODULE_NET_COMMON);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] Error loading NET_COMMON: 0x%08X\n", err);
        return 0;
    }

    err = sceUtilityLoadModule(PSP_MODULE_NET_INET);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] Error loading NET_INET: 0x%08X\n", err);
        return 0;
    }

    err = sceNetInit(128 * 1024, 42, 0x4000, 42, 0x4000);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] sceNetInit failed: 0x%08X\n", err);
        return 0;
    }

    err = sceNetInetInit();
    if (err < 0) {
        pspDebugScreenPrintf("[Net] sceNetInetInit failed: 0x%08X\n", err);
        return 0;
    }

    err = sceNetApctlInit(0x8000, 48);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] sceNetApctlInit failed: 0x%08X\n", err);
        return 0;
    }

    g_apctl_handler_id = sceNetApctlAddHandler(apctl_event_handler, NULL);
    return 1;
}

static int psp_wifi_connect_profile(const WifiProfile *profile) {
    int err;

    // Ensure APCTL is in DISCONNECTED state before starting a new connection
    int cur_state = 0;
    if (sceNetApctlGetState(&cur_state) == 0 && cur_state != PSP_NET_APCTL_STATE_DISCONNECTED) {
        sceNetApctlDisconnect();
        for (int i = 0; i < 40 && g_running; i++) {
            if (sceNetApctlGetState(&cur_state) == 0 && cur_state == PSP_NET_APCTL_STATE_DISCONNECTED) {
                break;
            }
            sceKernelDelayThread(50 * 1000);
        }
    }

    pspDebugScreenPrintf("\n[Net] Connecting to Profile #%d: '%s' (SSID: '%s')...\n",
                         profile->id, profile->name, profile->ssid);

    err = sceNetApctlConnect(profile->id);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] sceNetApctlConnect failed: 0x%08X\n", err);
        return 0;
    }

    int last_state = -1;
    int timeout_ticks = 0; // ~15s timeout (300 * 50ms)
    while (g_running) {
        int state = 0;
        err = sceNetApctlGetState(&state);
        if (err < 0) {
            pspDebugScreenPrintf("[Net] sceNetApctlGetState error: 0x%08X\n", err);
            return 0;
        }

        if (state != last_state) {
            pspDebugScreenPrintf("[Net] Connection state: %d / 4\n", state);

            if (state == PSP_NET_APCTL_STATE_DISCONNECTED && last_state > 0) {
                pspDebugScreenPrintf("[Net] Profile #%d ('%s') failed or is inactive.\n",
                                     profile->id, profile->ssid);
                return 0;
            }

            last_state = state;
            timeout_ticks = 0;
        }

        if (state == PSP_NET_APCTL_STATE_GOT_IP) {
            union SceNetApctlInfo info;
            if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) == 0) {
                pspDebugScreenPrintf("[Net] Connected! PSP IP: %s\n", info.ip);
            }
            return 1;
        }

        timeout_ticks++;
        if (timeout_ticks > 300) {
            pspDebugScreenPrintf("[Net] Connection timed out for '%s'.\n", profile->ssid);
            sceNetApctlDisconnect();
            return 0;
        }

        sceKernelDelayThread(50 * 1000);
    }

    return 0;
}

static int psp_wifi_select_and_connect(void) {
    if (!psp_wifi_init()) {
        return 0;
    }

    WifiProfile profiles[MAX_WIFI_PROFILES];
    int count = 0;

    for (int id = 1; id <= MAX_WIFI_PROFILES; id++) {
        if (sceUtilityCheckNetParam(id) == 0) {
            netData name_data, ssid_data;
            memset(&name_data, 0, sizeof(name_data));
            memset(&ssid_data, 0, sizeof(ssid_data));
            sceUtilityGetNetParam(id, PSP_NETPARAM_NAME, &name_data);
            sceUtilityGetNetParam(id, PSP_NETPARAM_SSID, &ssid_data);

            profiles[count].id = id;
            memcpy(profiles[count].name, name_data.asString, sizeof(profiles[count].name) - 1);
            profiles[count].name[sizeof(profiles[count].name) - 1] = '\0';
            memcpy(profiles[count].ssid, ssid_data.asString, sizeof(profiles[count].ssid) - 1);
            profiles[count].ssid[sizeof(profiles[count].ssid) - 1] = '\0';
            count++;
        }
    }

    if (count == 0) {
        pspDebugScreenPrintf("[Net] No saved Wi-Fi profiles found on this PSP!\n");
        pspDebugScreenPrintf("[Net] Configure a network in PSP Settings -> Network Settings.\n");
        return 0;
    }

    int selected = 0;
    SceCtrlData pad, old_pad;
    sceCtrlReadBufferPositive(&old_pad, 1);

    while (g_running) {
        pspDebugScreenClear();
        pspDebugScreenPrintf("==================================================\n");
        pspDebugScreenPrintf("   PSPRA - Select Wi-Fi Network                   \n");
        pspDebugScreenPrintf("==================================================\n");
        pspDebugScreenPrintf("[Info] Proxy Target: %s:%d\n\n", PSPRA_PROXY_IP, PSPRA_PROXY_PORT);
        pspDebugScreenPrintf("Controls:\n");
        pspDebugScreenPrintf("  [UP/DOWN] Select Profile\n");
        pspDebugScreenPrintf("  [X / O]   Connect to selected network\n");
        pspDebugScreenPrintf("  [SQUARE]  Auto-scan & connect to active network\n");
        pspDebugScreenPrintf("  [START]   Exit to XMB\n\n");
        pspDebugScreenPrintf("Saved Wi-Fi Profiles (%d found):\n", count);

        for (int i = 0; i < count; i++) {
            if (i == selected) {
                pspDebugScreenPrintf(" -> [#%d] %s (SSID: %s)\n",
                                     profiles[i].id, profiles[i].name, profiles[i].ssid);
            } else {
                pspDebugScreenPrintf("    [#%d] %s (SSID: %s)\n",
                                     profiles[i].id, profiles[i].name, profiles[i].ssid);
            }
        }

        // Wait for button input
        int action = 0; // 1 = connect selected, 2 = auto-scan all
        while (g_running) {
            sceCtrlReadBufferPositive(&pad, 1);
            unsigned int pressed = pad.Buttons & ~old_pad.Buttons;
            old_pad = pad;

            if (pressed & PSP_CTRL_UP) {
                selected = (selected - 1 + count) % count;
                break;
            }
            if (pressed & PSP_CTRL_DOWN) {
                selected = (selected + 1) % count;
                break;
            }
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) {
                action = 1;
                break;
            }
            if (pressed & PSP_CTRL_SQUARE) {
                action = 2;
                break;
            }
            if (pressed & PSP_CTRL_START) {
                return 0;
            }
            sceDisplayWaitVblankStart();
        }

        if (action == 1) {
            if (psp_wifi_connect_profile(&profiles[selected])) {
                return 1;
            }
            pspDebugScreenPrintf("\nPress [X/O] to pick another network, or [START] to exit.\n");
            while (g_running) {
                sceCtrlReadBufferPositive(&pad, 1);
                unsigned int pressed = pad.Buttons & ~old_pad.Buttons;
                old_pad = pad;
                if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE)) {
                    break;
                }
                if (pressed & PSP_CTRL_START) {
                    return 0;
                }
                sceDisplayWaitVblankStart();
            }
        } else if (action == 2) {
            pspDebugScreenPrintf("\n[Net] Auto-scanning all %d saved profiles...\n", count);
            for (int i = 0; i < count && g_running; i++) {
                if (psp_wifi_connect_profile(&profiles[i])) {
                    return 1;
                }
            }
            pspDebugScreenPrintf("\n[Net] None of the saved profiles connected.\n");
            pspDebugScreenPrintf("Press [X/O] to return to list, or [START] to exit.\n");
            while (g_running) {
                sceCtrlReadBufferPositive(&pad, 1);
                unsigned int pressed = pad.Buttons & ~old_pad.Buttons;
                old_pad = pad;
                if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE)) {
                    break;
                }
                if (pressed & PSP_CTRL_START) {
                    return 0;
                }
                sceDisplayWaitVblankStart();
            }
        }
    }

    return 0;
}

static void psp_wifi_shutdown(void) {
    if (g_apctl_handler_id >= 0) {
        sceNetApctlDelHandler(g_apctl_handler_id);
        g_apctl_handler_id = -1;
    }
    sceNetApctlDisconnect();
    sceNetApctlTerm();
    sceNetInetTerm();
    sceNetTerm();
    sceUtilityUnloadModule(PSP_MODULE_NET_INET);
    sceUtilityUnloadModule(PSP_MODULE_NET_COMMON);
}

// ============================================================================
// 3. MAIN ENTRY POINT
// ============================================================================
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    pspDebugScreenPrintf("==================================================\n");
    pspDebugScreenPrintf("   PSPRA - PlayStation Portable Client Runner     \n");
    pspDebugScreenPrintf("==================================================\n");
    pspDebugScreenPrintf("[Info] Proxy Target: %s:%d\n", PSPRA_PROXY_IP, PSPRA_PROXY_PORT);

    uint8_t *dummy_ram = (uint8_t *)malloc(DUMMY_RAM_SIZE);
    if (!dummy_ram) {
        pspDebugScreenPrintf("[Fatal] Failed to allocate 128 KB dummy RAM!\n");
        sceKernelDelayThread(3000000);
        sceKernelExitGame();
        return 0;
    }
    memset(dummy_ram, 0, DUMMY_RAM_SIZE);

    if (psp_wifi_select_and_connect()) {
        if (pspra_init(dummy_ram, DUMMY_RAM_SIZE)) {
            pspra_login(PSPRA_RA_USER, PSPRA_RA_PASS);
            pspra_load_game(TEST_ROM_HASH);

            // Baseline SMW WRAM State (Active Gameplay in Yoshi's Island 2)
            dummy_ram[0x0100] = 0x14; // Game Mode: In-Level Gameplay
            dummy_ram[0x009D] = 0x00; // Sprites/Game unpaused
            dummy_ram[0x13BF] = 0x29; // Translevel: Yoshi's Island 2
            dummy_ram[0x1420] = 4;    // 4/5 Dragon Coins collected
            dummy_ram[0x1422] = 4;    // 4/5 Dragon Coins collected
            dummy_ram[0x0DBE] = 97;   // 98 Lives in status bar
            dummy_ram[0x0DB4] = 97;   // 98 Lives in status bar

            // Prime rcheevos delta buffers for 10 frames
            for (int i = 0; i < 10 && g_running; i++) {
                pspra_update_frame();
                sceDisplayWaitVblankStart();
            }

            pspDebugScreenPrintf("\n==================================================\n");
            pspDebugScreenPrintf("   SMW Interactive Achievement Trigger Ready      \n");
            pspDebugScreenPrintf("   [CROSS (X)] : Inject SMW RAM Trigger           \n");
            pspDebugScreenPrintf("   [START]     : Exit to XMB                      \n");
            pspDebugScreenPrintf("==================================================\n");

            SceCtrlData pad, old_pad;
            sceCtrlReadBufferPositive(&old_pad, 1);

            while (g_running) {
                sceCtrlReadBufferPositive(&pad, 1);
                unsigned int pressed = pad.Buttons & ~old_pad.Buttons;
                old_pad = pad;

                if (pressed & PSP_CTRL_CROSS) {
                    pspDebugScreenPrintf("[Test] CROSS pressed! Injecting SMW RAM states...\n");

                    // Frame N (Pre-condition)
                    dummy_ram[0x0100] = 0x14;
                    dummy_ram[0x1420] = 4;
                    dummy_ram[0x1422] = 4;
                    dummy_ram[0x0DBE] = 97;
                    dummy_ram[0x0DB4] = 97;
                    dummy_ram[0x0019] = 0x00;
                    pspra_update_frame();

                    // Frame N+1 (Trigger Mutation)
                    dummy_ram[0x1420] = 5;    // 5th Dragon Coin
                    dummy_ram[0x1422] = 5;    // 5th Dragon Coin
                    dummy_ram[0x0DBE] = 98;   // 99 Lives
                    dummy_ram[0x0DB4] = 98;   // 99 Lives
                    dummy_ram[0x0019] = 0x02; // Cape Mario power-up
                    pspra_update_frame();

                    pspDebugScreenPrintf("[Test] SMW RAM Trigger injected (5 Dragon Coins, 99 Lives, Cape Mario)!\n");
                }

                if (pressed & PSP_CTRL_START) {
                    break;
                }

                pspra_update_frame();
                sceDisplayWaitVblankStart();
            }
        }
    } else {
        pspDebugScreenPrintf("[Fatal] Wi-Fi connection cancelled or failed.\n");
        pspDebugScreenPrintf("\nPress [START] to exit to XMB.\n");
        SceCtrlData pad;
        while (g_running) {
            sceCtrlReadBufferPositive(&pad, 1);
            if (pad.Buttons & PSP_CTRL_START) {
                break;
            }
            sceDisplayWaitVblankStart();
        }
    }

    pspDebugScreenPrintf("[Runner] Cleaning up and exiting...\n");
    pspra_destroy();
    free(dummy_ram);
    psp_wifi_shutdown();
    sceKernelExitGame();
    return 0;
}