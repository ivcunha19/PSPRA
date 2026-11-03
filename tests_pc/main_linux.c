#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h> // For usleep()

#include "credentials.h"
#include "pspra_client.h"

// Simulated 128 KB of SNES Work RAM (WRAM)
#define DUMMY_RAM_SIZE 131072
static uint8_t g_dummy_ram[DUMMY_RAM_SIZE];

// Official RetroAchievements MD5 hash for Super Mario World (SNES - USA)
#define TEST_ROM_HASH "cdd3c8c37322978ca8669b34bc89c804"

int main(void) {
    printf("==================================================\n");
    printf("   PSPRA - Linux Local Integration Test Runner    \n");
    printf("==================================================\n");

    // 1. Clear simulated RAM
    memset(g_dummy_ram, 0, sizeof(g_dummy_ram));

    // 2. Initialize the PSPRA client
    if (!pspra_init(g_dummy_ram, DUMMY_RAM_SIZE)) {
        printf("[Test Runner] Fatal: Could not initialize PSPRA.\n");
        return 1;
    }

    // 3. Authenticate with RetroAchievements via the local Python proxy
    pspra_login(PSPRA_RA_USER, PSPRA_RA_PASS);

    // 4. Load achievement definitions for Super Mario World
    pspra_load_game(TEST_ROM_HASH);

    // 5. Simulate an emulator running at 60 FPS for 2 seconds (120 frames)
    printf("[Test Runner] Running 120 emulated frames (60 FPS)...\n");
    for (int frame = 0; frame < 120; frame++) {
        if (frame == 10) {
            printf("[Test Runner] Frame 10: Mario enters level (Small Mario, no Yoshi)...\n");
            g_dummy_ram[0x0100] = 0x14; // Game Mode: 0x14 (In active level)
            g_dummy_ram[0x0071] = 0x00; // Player Animation: Normal
            g_dummy_ram[0x13D4] = 0x00; // Pause flag: Unpaused
            g_dummy_ram[0x1426] = 0x00; // Message box: None
            g_dummy_ram[0x0019] = 0x00; // Power-up state: 0 (Small Mario)
            g_dummy_ram[0x187A] = 0x00; // Riding Yoshi: 0 (No)
        }

        if (frame == 60) {
            printf("[Test Runner] Frame 60: Mario collects a Feather and hops on Yoshi!\n");
            g_dummy_ram[0x0019] = 0x02; // Power-up state: 2 (Cape / Feather -> ID 4874)
            g_dummy_ram[0x187A] = 0x01; // Riding Yoshi: 1 (Riding -> ID 342)
            g_dummy_ram[0x0DC1] = 0x01; // Carrying Yoshi across levels flag
        }

        pspra_update_frame();
        usleep(16666); // ~16.6ms per frame (60 Hz)
    }

    // 6. Clean up resources
    printf("[Test Runner] Shutting down PSPRA client...\n");
    pspra_destroy();

    printf("[Test Runner] Test completed successfully!\n");
    return 0;
}