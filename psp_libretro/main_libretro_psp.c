#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psputility.h>
#include <psppower.h>
#include <pspaudio.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <libretro.h>

#include "credentials.h"
#include "pspra_client.h"

PSP_MODULE_INFO("PSPRA_Libretro_SNES", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(20 * 1024);

#define PSP_SCREEN_WIDTH  480
#define PSP_SCREEN_HEIGHT 272
#define PSP_VRAM_STRIDE   512
#define PSP_VRAM_BASE     ((uint16_t *)0x44000000)

#define SYSTEM_DIR_PATH   "ms0:/PSP/GAME/PSPRA_SNES"
#define AUDIO_SAMPLES     512

#define EXIT_COMBO_MASK   (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START | PSP_CTRL_SELECT)

static volatile int g_running = 1;
static int g_in_gameplay = 0;
static SceCtrlData g_pad;
static int16_t g_joypad_mask = 0;
static int g_av_enable = 1 | 2; // Bit 0: Video, Bit 1: Audio
static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;

static int g_audio_ch = -1;
static int g_audio_buf_idx = 0;
static int16_t __attribute__((aligned(64))) g_audio_buf[2][AUDIO_SAMPLES * 2];

// ============================================================================
// COMPACT 8x8 ASCII BITMAP FONT (0x20 .. 0x7E) FOR OSD NOTIFICATION BANNER
// ============================================================================
static const uint8_t g_font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // '#'
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, // '$'
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, // '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // '/'
    {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00}, // '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // '1'
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00}, // '2'
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // '3'
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, // '4'
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, // '5'
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // '6'
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, // '7'
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // '8'
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, // '9'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // ':'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // ';'
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // '<'
    {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00}, // '='
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, // '>'
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, // '?'
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00}, // '@'
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'A'
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, // 'B'
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, // 'C'
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, // 'D'
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, // 'E'
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, // 'F'
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, // 'G'
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'H'
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'I'
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, // 'J'
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, // 'K'
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, // 'L'
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00}, // 'M'
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 'N'
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 'O'
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, // 'P'
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06}, // 'Q'
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, // 'R'
    {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00}, // 'S'
    {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00}, // 'T'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 'U'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 'V'
    {0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00}, // 'W'
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, // 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, // 'Y'
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, // 'Z'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // '\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, // 'a'
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, // 'b'
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, // 'c'
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, // 'd'
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, // 'e'
    {0x3C,0x66,0x60,0xF8,0x60,0x60,0xF0,0x00}, // 'f'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, // 'g'
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, // 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 'i'
    {0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C}, // 'j'
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, // 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'l'
    {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00}, // 'm'
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, // 'n'
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, // 'o'
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, // 'p'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, // 'q'
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, // 'r'
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00}, // 's'
    {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00}, // 't'
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, // 'u'
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 'v'
    {0x00,0x00,0xC6,0xD6,0xFE,0xFE,0x6C,0x00}, // 'w'
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 'x'
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC}, // 'y'
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00}, // 'z'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  // '~'
};

static void osd_draw_char(uint16_t *vram, int x, int y, char c, uint16_t color) {
    if (c < 0x20 || c > 0x7E) {
        c = '?';
    }
    const uint8_t *glyph = g_font8x8[(int)(c - 0x20)];
    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || py >= PSP_SCREEN_HEIGHT) continue;
        uint8_t bits = glyph[row];
        uint16_t *dst = &vram[py * PSP_VRAM_STRIDE];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int px = x + col;
                if (px >= 0 && px < PSP_SCREEN_WIDTH) {
                    dst[px] = color;
                }
            }
        }
    }
}

static void osd_draw_banner(uint16_t *vram, const char *text) {
    // Dark background bar across y = 0..16 with a gold bottom accent line
    const uint16_t bg_color     = 0x2082; // Dark blue-grey in BGR565
    const uint16_t border_color = 0x06BF; // Gold accent in BGR565
    const uint16_t text_color   = 0x07FF; // Bright yellow/gold in BGR565

    for (int y = 0; y < 16; y++) {
        uint16_t *row = &vram[y * PSP_VRAM_STRIDE];
        for (int x = 0; x < PSP_SCREEN_WIDTH; x++) {
            row[x] = bg_color;
        }
    }
    uint16_t *border_row = &vram[16 * PSP_VRAM_STRIDE];
    for (int x = 0; x < PSP_SCREEN_WIDTH; x++) {
        border_row[x] = border_color;
    }

    int cx = 6;
    for (const char *p = text; *p != '\0' && (cx + 8) <= PSP_SCREEN_WIDTH; p++) {
        osd_draw_char(vram, cx, 4, *p, text_color);
        cx += 8;
    }
}

static void osd_clear_banner(uint16_t *vram) {
    for (int y = 0; y <= 16; y++) {
        memset(&vram[y * PSP_VRAM_STRIDE], 0, PSP_SCREEN_WIDTH * sizeof(uint16_t));
    }
}

// ============================================================================
// 1. PSP EXIT CALLBACK (HOME BUTTON)
// ============================================================================
static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    g_running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static int setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, 0);
    }
    return thid;
}

// ============================================================================
// 2. PSP WI-FI INITIALIZATION & INTERACTIVE SELECTION MENU
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
    if (error != 0 && !g_in_gameplay) {
        pspDebugScreenPrintf("[Net] Apctl Error: 0x%08X (state %d -> %d, event %d)\n",
                             (unsigned int)error, oldState, newState, event);
    }
}

static int psp_wifi_init(void) {
    int err;

    pspDebugScreenPrintf("[Net] Loading PSP network modules...\n");
    err = sceUtilityLoadModule(PSP_MODULE_NET_COMMON);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] Error loading NET_COMMON: 0x%08X\n", (unsigned int)err);
        return 0;
    }

    err = sceUtilityLoadModule(PSP_MODULE_NET_INET);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] Error loading NET_INET: 0x%08X\n", (unsigned int)err);
        return 0;
    }

    err = sceNetInit(128 * 1024, 42, 0x4000, 42, 0x4000);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] sceNetInit failed: 0x%08X\n", (unsigned int)err);
        return 0;
    }

    err = sceNetInetInit();
    if (err < 0) {
        pspDebugScreenPrintf("[Net] sceNetInetInit failed: 0x%08X\n", (unsigned int)err);
        return 0;
    }

    err = sceNetApctlInit(0x8000, 48);
    if (err < 0) {
        pspDebugScreenPrintf("[Net] sceNetApctlInit failed: 0x%08X\n", (unsigned int)err);
        return 0;
    }

    g_apctl_handler_id = sceNetApctlAddHandler(apctl_event_handler, NULL);
    return 1;
}

static int psp_wifi_connect_profile(const WifiProfile *profile) {
    int err;

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
        pspDebugScreenPrintf("[Net] sceNetApctlConnect failed: 0x%08X\n", (unsigned int)err);
        return 0;
    }

    int last_state = -1;
    int timeout_ticks = 0;
    while (g_running) {
        int state = 0;
        err = sceNetApctlGetState(&state);
        if (err < 0) {
            pspDebugScreenPrintf("[Net] sceNetApctlGetState error: 0x%08X\n", (unsigned int)err);
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

// Returns: 1 = connected, 0 = start offline, -1 = exit to XMB
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
        pspDebugScreenPrintf("   PSPRA SNES9x2005 - Select Wi-Fi Network        \n");
        pspDebugScreenPrintf("==================================================\n");
        pspDebugScreenPrintf("[Info] CPU Clock: 333 MHz | Proxy: %s:%d\n\n",
                             PSPRA_PROXY_IP, PSPRA_PROXY_PORT);
        pspDebugScreenPrintf("Controls:\n");
        pspDebugScreenPrintf("  [UP/DOWN]  Select Profile\n");
        pspDebugScreenPrintf("  [X / O]    Connect to selected network\n");
        pspDebugScreenPrintf("  [SQUARE]   Auto-scan & connect to active network\n");
        pspDebugScreenPrintf("  [TRIANGLE] Start game offline\n");
        pspDebugScreenPrintf("  [START]    Exit to XMB\n\n");
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
            if (pressed & PSP_CTRL_TRIANGLE) {
                return 0;
            }
            if (pressed & PSP_CTRL_START) {
                return -1;
            }
            sceDisplayWaitVblankStart();
        }

        if (action == 1) {
            if (psp_wifi_connect_profile(&profiles[selected])) {
                return 1;
            }
            pspDebugScreenPrintf("\nPress [X/O] to pick another network, [TRIANGLE] offline, or [START] to exit.\n");
            while (g_running) {
                sceCtrlReadBufferPositive(&pad, 1);
                unsigned int pressed = pad.Buttons & ~old_pad.Buttons;
                old_pad = pad;
                if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE)) {
                    break;
                }
                if (pressed & PSP_CTRL_TRIANGLE) {
                    return 0;
                }
                if (pressed & PSP_CTRL_START) {
                    return -1;
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
            pspDebugScreenPrintf("Press [X/O] to return to list, [TRIANGLE] offline, or [START] to exit.\n");
            while (g_running) {
                sceCtrlReadBufferPositive(&pad, 1);
                unsigned int pressed = pad.Buttons & ~old_pad.Buttons;
                old_pad = pad;
                if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE)) {
                    break;
                }
                if (pressed & PSP_CTRL_TRIANGLE) {
                    return 0;
                }
                if (pressed & PSP_CTRL_START) {
                    return -1;
                }
                sceDisplayWaitVblankStart();
            }
        }
    }

    return -1;
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
// 3. LIBRETRO FRONTEND CALLBACKS
// ============================================================================
static void retro_log_printf(enum retro_log_level level, const char *fmt, ...) {
    (void)level;
    if (g_in_gameplay) {
        return;
    }
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    pspDebugScreenPrintf("[Core] %s", buf);
}

static bool environment_cb(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback *cb = (struct retro_log_callback *)data;
            if (cb) {
                cb->log = retro_log_printf;
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const enum retro_pixel_format *fmt = (const enum retro_pixel_format *)data;
            if (!fmt) {
                return false;
            }
            if (*fmt == RETRO_PIXEL_FORMAT_RGB565 || *fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
                g_pixel_format = *fmt;
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            const char **dir = (const char **)data;
            if (dir) {
                *dir = SYSTEM_DIR_PATH;
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            const char **dir = (const char **)data;
            if (dir) {
                *dir = SYSTEM_DIR_PATH;
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return true;

        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: {
            int *val = (int *)data;
            if (val) {
                *val = g_av_enable;
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

static void video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
    uint16_t *vram = PSP_VRAM_BASE;
    static int s_banner_was_active = 0;

    if (data != NULL && width > 0 && height > 0) {
        const uint16_t *src = (const uint16_t *)data;
        size_t src_stride = pitch >> 1;

        unsigned copy_w = (width > PSP_SCREEN_WIDTH) ? PSP_SCREEN_WIDTH : width;
        unsigned copy_h = (height > PSP_SCREEN_HEIGHT) ? PSP_SCREEN_HEIGHT : height;
        unsigned offset_x = ((PSP_SCREEN_WIDTH - copy_w) >> 1) & ~1u;
        unsigned offset_y = (PSP_SCREEN_HEIGHT - copy_h) >> 1;

        if (g_pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
            unsigned pairs = copy_w >> 1;
            unsigned pairs4 = pairs & ~3u;
            for (unsigned y = 0; y < copy_h; y++) {
                const uint32_t *src32 = (const uint32_t *)&src[y * src_stride];
                uint32_t *dst32 = (uint32_t *)&vram[(offset_y + y) * PSP_VRAM_STRIDE + offset_x];
                unsigned x = 0;
                // Unrolled 32-bit burst blitter (8 pixels = 4x uint32_t words per iteration)
                for (; x < pairs4; x += 4) {
                    uint32_t p0 = src32[x + 0];
                    uint32_t p1 = src32[x + 1];
                    uint32_t p2 = src32[x + 2];
                    uint32_t p3 = src32[x + 3];
                    dst32[x + 0] = ((p0 & 0x001F001Fu) << 11) | (p0 & 0x07E007E0u) | ((p0 >> 11) & 0x001F001Fu);
                    dst32[x + 1] = ((p1 & 0x001F001Fu) << 11) | (p1 & 0x07E007E0u) | ((p1 >> 11) & 0x001F001Fu);
                    dst32[x + 2] = ((p2 & 0x001F001Fu) << 11) | (p2 & 0x07E007E0u) | ((p2 >> 11) & 0x001F001Fu);
                    dst32[x + 3] = ((p3 & 0x001F001Fu) << 11) | (p3 & 0x07E007E0u) | ((p3 >> 11) & 0x001F001Fu);
                }
                for (; x < pairs; x++) {
                    uint32_t p = src32[x];
                    dst32[x] = ((p & 0x001F001Fu) << 11) | (p & 0x07E007E0u) | ((p >> 11) & 0x001F001Fu);
                }
            }
        } else {
            unsigned pairs = copy_w >> 1;
            for (unsigned y = 0; y < copy_h; y++) {
                const uint32_t *src32 = (const uint32_t *)&src[y * src_stride];
                uint32_t *dst32 = (uint32_t *)&vram[(offset_y + y) * PSP_VRAM_STRIDE + offset_x];
                for (unsigned x = 0; x < pairs; x++) {
                    uint32_t p = src32[x];
                    uint32_t r = (p >> 10) & 0x001F001Fu;
                    uint32_t g = (p >> 5)  & 0x001F001Fu;
                    uint32_t b =  p        & 0x001F001Fu;
                    dst32[x] = (b << 11) | (g << 6) | r;
                }
            }
        }
    }

    const char *notif = pspra_get_active_notification();
    if (notif != NULL) {
        osd_draw_banner(vram, notif);
        s_banner_was_active = 1;
    } else if (s_banner_was_active) {
        osd_clear_banner(vram);
        s_banner_was_active = 0;
    }
}

static void audio_sample_cb(int16_t left, int16_t right) {
    (void)left;
    (void)right;
}

static size_t audio_sample_batch_cb(const int16_t *data, size_t frames) {
    if (g_audio_ch >= 0 && data != NULL && frames >= AUDIO_SAMPLES) {
        // Non-blocking check: only submit when the hardware channel is ready
        if (sceAudioGetChannelRestLen(g_audio_ch) == 0) {
            memcpy(g_audio_buf[g_audio_buf_idx], data, AUDIO_SAMPLES * 2 * sizeof(int16_t));
            sceAudioOutput(g_audio_ch, PSP_AUDIO_VOLUME_MAX, g_audio_buf[g_audio_buf_idx]);
            g_audio_buf_idx ^= 1;
        }
    }
    return frames;
}

static void input_poll_cb(void) {
    sceCtrlReadBufferPositive(&g_pad, 1);

    uint32_t btns = g_pad.Buttons;
    int16_t mask = 0;

    if (btns & PSP_CTRL_CROSS)    mask |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (btns & PSP_CTRL_SQUARE)   mask |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (btns & PSP_CTRL_CIRCLE)   mask |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (btns & PSP_CTRL_TRIANGLE) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (btns & PSP_CTRL_START)    mask |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (btns & PSP_CTRL_SELECT)   mask |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (btns & PSP_CTRL_LTRIGGER) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (btns & PSP_CTRL_RTRIGGER) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_R);

    if ((btns & PSP_CTRL_UP)    || g_pad.Ly < 64)  mask |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if ((btns & PSP_CTRL_DOWN)  || g_pad.Ly > 192) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if ((btns & PSP_CTRL_LEFT)  || g_pad.Lx < 64)  mask |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if ((btns & PSP_CTRL_RIGHT) || g_pad.Lx > 192) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);

    g_joypad_mask = mask;
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) {
        return 0;
    }
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        return g_joypad_mask;
    }
    if (id < 16) {
        return (g_joypad_mask & (1 << id)) ? 1 : 0;
    }
    return 0;
}

// ============================================================================
// 4. MAIN ENTRY POINT
// ============================================================================
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    // 1. Lock PSP CPU/Bus clock to 333/166 MHz for full-speed 60 FPS SNES emulation
    scePowerSetClockFrequency(333, 333, 166);

    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    pspDebugScreenPrintf("==================================================\n");
    pspDebugScreenPrintf("   PSPRA Libretro SNES Frontend (Snes9x 2005)     \n");
    pspDebugScreenPrintf("==================================================\n");
    pspDebugScreenPrintf("[Info] CPU Clock: 333 MHz | Proxy: %s:%d\n",
                         PSPRA_PROXY_IP, PSPRA_PROXY_PORT);

    // 2. Interactive Wi-Fi network selection menu (like psp_sample)
    int wifi_status = psp_wifi_select_and_connect();
    if (wifi_status < 0) {
        psp_wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }
    int wifi_connected = (wifi_status == 1);
    if (!wifi_connected) {
        pspDebugScreenPrintf("\n[Net] Starting game in offline mode.\n");
    }

    // 3. Read "rom.sfc" from the current directory
    FILE *rom_fp = fopen("rom.sfc", "rb");
    if (!rom_fp) {
        rom_fp = fopen(SYSTEM_DIR_PATH "/rom.sfc", "rb");
    }
    if (!rom_fp) {
        pspDebugScreenPrintf("[Fatal] Failed to open rom.sfc!\n");
        pspDebugScreenPrintf("Place 'rom.sfc' in the same folder as EBOOT.PBP.\n");
        sceKernelDelayThread(4000000);
        psp_wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }

    fseek(rom_fp, 0, SEEK_END);
    long rom_file_len = ftell(rom_fp);
    fseek(rom_fp, 0, SEEK_SET);

    if (rom_file_len <= 0) {
        fclose(rom_fp);
        pspDebugScreenPrintf("[Fatal] Invalid rom.sfc file size!\n");
        sceKernelDelayThread(3000000);
        psp_wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }

    size_t rom_size = (size_t)rom_file_len;
    uint8_t *rom_buf = (uint8_t *)malloc(rom_size);
    if (!rom_buf) {
        fclose(rom_fp);
        pspDebugScreenPrintf("[Fatal] Failed to allocate %u bytes for ROM!\n", (unsigned int)rom_size);
        sceKernelDelayThread(3000000);
        psp_wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }

    if (fread(rom_buf, 1, rom_size, rom_fp) != rom_size) {
        free(rom_buf);
        fclose(rom_fp);
        pspDebugScreenPrintf("[Fatal] Failed to read rom.sfc into memory!\n");
        sceKernelDelayThread(3000000);
        psp_wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }
    fclose(rom_fp);

    // 4. Compute SNES MD5 hash (stripping 512-byte SMC header if present)
    char rom_md5[33];
    pspra_compute_snes_md5(rom_buf, rom_size, rom_md5);
    pspDebugScreenPrintf("[ROM] Loaded rom.sfc, MD5: %s\n", rom_md5);

    // 5. Initialize Libretro callbacks and Snes9x 2005 core
    retro_set_environment(environment_cb);
    retro_set_video_refresh(video_refresh_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_sample_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    struct retro_game_info game_info;
    memset(&game_info, 0, sizeof(game_info));
    game_info.path = "rom.sfc";
    game_info.data = rom_buf;
    game_info.size = rom_size;
    game_info.meta = NULL;

    if (!retro_load_game(&game_info)) {
        free(rom_buf);
        retro_deinit();
        pspDebugScreenPrintf("[Fatal] retro_load_game failed for rom.sfc!\n");
        sceKernelDelayThread(3000000);
        psp_wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }

    // Snes9x 2005 copies the ROM into its internal Memory.ROM buffer during retro_load_game
    free(rom_buf);
    rom_buf = NULL;

    // 6. Fetch SNES 128 KB Work RAM pointer & size from Libretro and initialize PSPRA
    uint8_t *wram = (uint8_t *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t wram_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    pspDebugScreenPrintf("[Core] SNES WRAM: %p (%u bytes)\n",
                         (void *)wram, (unsigned int)wram_size);

    if (wifi_connected && wram != NULL && wram_size > 0) {
        if (pspra_init(wram, (uint32_t)wram_size)) {
            pspra_login(PSPRA_RA_USER, PSPRA_RA_PASS);
            pspra_load_game(rom_md5);
        }
    }

    // 7. Reserve PSP hardware audio channel (non-blocking stereo 512 samples)
    g_audio_ch = sceAudioChReserve(0, AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO);

    // 8. Re-assert 333/166 MHz clock, switch display to 16-bit RGB565, and enter 60 FPS loop
    scePowerSetClockFrequency(333, 333, 166);
    g_in_gameplay = 1;
    pspDebugScreenSetOffset(PSP_VRAM_STRIDE * PSP_SCREEN_HEIGHT * 2);
    memset(PSP_VRAM_BASE, 0, PSP_VRAM_STRIDE * PSP_SCREEN_HEIGHT * sizeof(uint16_t));

    sceDisplaySetMode(0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
    sceDisplaySetFrameBuf((void *)0x44000000, PSP_VRAM_STRIDE,
                          PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);

    uint32_t target_us = sceKernelGetSystemTimeLow();
    int skip_count = 0;

    while (g_running) {
        target_us += 16667; // 60.0 FPS (16.667 ms per frame)

        retro_run();
        pspra_update_frame();

        uint32_t now_us = sceKernelGetSystemTimeLow();
        int32_t ahead_us = (int32_t)(target_us - now_us);

        if (ahead_us > 0) {
            g_av_enable = 1 | 2;
            skip_count = 0;
            if (ahead_us > 1500) {
                sceKernelDelayThread((SceUInt)(ahead_us - 500));
            }
        } else {
            int32_t behind_us = -ahead_us;
            if (behind_us > 3500 && skip_count < 2) {
                // Skip PPU rendering for 1 frame so CPU/APU/RA stay locked at 60 Hz
                g_av_enable = 2;
                skip_count++;
            } else {
                g_av_enable = 1 | 2;
                skip_count = 0;
            }
            if (behind_us > 50000) {
                target_us = now_us;
            }
        }

        // Exit combo: L + R + START + SELECT
        if ((g_pad.Buttons & EXIT_COMBO_MASK) == EXIT_COMBO_MASK) {
            g_running = 0;
        }
    }

    // 9. Clean shutdown
    g_in_gameplay = 0;
    if (g_audio_ch >= 0) {
        sceAudioChRelease(g_audio_ch);
        g_audio_ch = -1;
    }

    retro_unload_game();
    retro_deinit();
    pspra_destroy();
    psp_wifi_shutdown();

    sceKernelExitGame();
    return 0;
}
