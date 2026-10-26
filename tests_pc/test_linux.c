#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h> // For usleep()

#include "credentials.h"
#include "pspra_client.h"

// Simulated 64 KB of emulator RAM
#define DUMMY_RAM_SIZE 65536
static uint8_t g_dummy_ram[DUMMY_RAM_SIZE];

// Official RetroAchievements MD5 hash for Super Mario Bros. (NES)
#define TEST_ROM_HASH "555f9f13f1f0f5c4138a3b2a58c34c88"

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
    // Note: Because pspRaServerCall is synchronous over TCP, the login callback
    // will finish executing before pspra_login() returns!
    pspra_login(PSPRA_RA_USER, PSPRA_RA_PASS);

    // 4. Load achievement definitions for the test game
    pspra_load_game(TEST_ROM_HASH);

    // 5. Simulate an emulator running at 60 FPS for 5 seconds (300 frames)
    printf("[Test Runner] Running 300 emulated frames (60 FPS)...\n");
    for (int frame = 0; frame < 300; frame++) {
        pspra_update_frame();
        usleep(16666); // ~16.6ms per frame (60 Hz)
    }

    // 6. Clean up resources
    printf("[Test Runner] Shutting down PSPRA client...\n");
    pspra_destroy();

    printf("[Test Runner] Test completed successfully!\n");
    return 0;
}