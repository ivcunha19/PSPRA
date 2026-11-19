#ifndef PSPRA_CONFIG_H
#define PSPRA_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char username[64];
    char password[64]; // Supports password or API/Connect token
    char proxy_ip[64];
    int proxy_port;
    int wifi_profile;  // 1-based PSP Netconf utility profile index (default: 1)
    int hardcore_mode; // 0 or 1 (default: 0)
    int video_scale;   // 0 = 1:1, 1 = 4:3, 2 = Fullscreen (default: 1)
    int video_filter;  // 0 = Nearest/Sharp, 1 = Bilinear/Smooth (default: 1)
} pspra_config_t;

/**
 * Returns a pointer to the global singleton configuration struct.
 */
const pspra_config_t* pspra_config_get(void);

/**
 * Loads configuration from custom_path (if non-NULL) or standard fallback paths:
 *   1. "config.ini"
 *   2. "ms0:/PSP/GAME/PSPRA/config.ini"
 *   3. "ms0:/seplugins/pspra/config.ini"
 *   4. "ef0:/seplugins/pspra/config.ini"
 *
 * Returns 1 if a configuration file was loaded with valid credentials,
 * or 0 if no file was found or credentials are missing/placeholders.
 */
int pspra_config_load(const char* custom_path);

/**
 * Creates a template config.ini file at the given path with placeholder values.
 * Returns 1 on success, 0 on failure.
 */
int pspra_config_create_template(const char* path);

/**
 * Returns 1 if the current configuration has non-empty, non-placeholder credentials.
 */
int pspra_config_has_valid_credentials(void);

#ifdef __cplusplus
}
#endif

#endif // PSPRA_CONFIG_H
