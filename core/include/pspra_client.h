#ifndef PSPRA_CLIENT_H
#define PSPRA_CLIENT_H

#include <stdint.h>

// 1. Initializes rcheevos and stores the pointer to the emulator's RAM
int pspra_init(uint8_t* ram_pointer, uint32_t ram_size);

// 2. Logs in using the username and password (or API token)
void pspra_login(const char* username, const char* password);

// 3. Loads game achievements using the ROM's MD5 hash
void pspra_load_game(const char* rom_hash);

// 4. Must be called 60 times per second (once per frame) to evaluate RAM
void pspra_update_frame(void);

// 5. Shuts down the client and frees memory when closing the game
void pspra_destroy(void);

#endif