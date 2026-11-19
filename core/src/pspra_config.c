#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
#define PSPRA_LOG(...) pspDebugScreenPrintf(__VA_ARGS__)
#else
#define PSPRA_LOG(...) printf(__VA_ARGS__)
#endif

#include "pspra_config.h"

static pspra_config_t g_config = {
    .username      = "",
    .password      = "",
    .proxy_ip      = "127.0.0.1",
    .proxy_port    = 8080,
    .wifi_profile  = 1,
    .hardcore_mode = 0,
    .video_scale   = 1,
    .video_filter  = 1
};

static char* trim_whitespace(char* str) {
    if (!str) {
        return str;
    }

    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    // Strip optional matching quotes around value
    size_t len = strlen(str);
    if (len >= 2 && ((str[0] == '"' && str[len - 1] == '"') ||
                     (str[0] == '\'' && str[len - 1] == '\''))) {
        str[len - 1] = '\0';
        str++;
    }

    return str;
}

static int is_placeholder_or_empty(const char* str) {
    if (!str || str[0] == '\0') {
        return 1;
    }
    if (strcmp(str, "YOUR_USERNAME") == 0 ||
        strcmp(str, "YOUR_PASSWORD") == 0 ||
        strcmp(str, "YOUR_RA_USERNAME") == 0 ||
        strcmp(str, "YOUR_RA_PASSWORD") == 0) {
        return 1;
    }
    return 0;
}

const pspra_config_t* pspra_config_get(void) {
    return &g_config;
}

int pspra_config_has_valid_credentials(void) {
    return (!is_placeholder_or_empty(g_config.username) &&
            !is_placeholder_or_empty(g_config.password));
}

int pspra_config_create_template(const char* path) {
    const char* target_path = (path && path[0] != '\0') ? path : "config.ini";

    FILE* fp = fopen(target_path, "w");
    if (!fp) {
        PSPRA_LOG("[Config] Error: Failed to create template %s.\n", target_path);
        return 0;
    }

    fprintf(fp, "# PSPRA Runtime Configuration\n");
    fprintf(fp, "[retroachievements]\n");
    fprintf(fp, "username = YOUR_USERNAME\n");
    fprintf(fp, "password = YOUR_PASSWORD\n");
    fprintf(fp, "hardcore_mode = 0\n\n");
    fprintf(fp, "[network]\n");
    fprintf(fp, "proxy_ip = 192.168.1.100\n");
    fprintf(fp, "proxy_port = 8080\n");
    fprintf(fp, "wifi_profile = 1\n\n");
    fprintf(fp, "[video]\n");
    fprintf(fp, "video_scale = 1\n");
    fprintf(fp, "video_filter = 1\n");
    fclose(fp);

    PSPRA_LOG("[Config] Created template config.ini. Please edit it with your RetroAchievements credentials.\n");
    return 1;
}

int pspra_config_load(const char* custom_path) {
    // Reset to safe defaults before parsing
    g_config.username[0] = '\0';
    g_config.password[0] = '\0';
    strncpy(g_config.proxy_ip, "127.0.0.1", sizeof(g_config.proxy_ip) - 1);
    g_config.proxy_ip[sizeof(g_config.proxy_ip) - 1] = '\0';
    g_config.proxy_port    = 8080;
    g_config.wifi_profile  = 1;
    g_config.hardcore_mode = 0;
    g_config.video_scale   = 1;
    g_config.video_filter  = 1;

    static const char* const fallback_paths[] = {
        "config.ini",
        "ms0:/PSP/GAME/PSPRA/config.ini",
        "ms0:/seplugins/pspra/config.ini",
        "ef0:/seplugins/pspra/config.ini"
    };

    FILE* fp = NULL;
    const char* loaded_path = NULL;

    if (custom_path != NULL && custom_path[0] != '\0') {
        fp = fopen(custom_path, "r");
        if (fp) {
            loaded_path = custom_path;
        }
    }

    if (!fp) {
        for (size_t i = 0; i < sizeof(fallback_paths) / sizeof(fallback_paths[0]); i++) {
            fp = fopen(fallback_paths[i], "r");
            if (fp) {
                loaded_path = fallback_paths[i];
                break;
            }
        }
    }

    if (!fp) {
        PSPRA_LOG("[Config] Warning: No config.ini found in search paths.\n");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Strip inline comments (# or ;)
        for (char* p = line; *p != '\0'; p++) {
            if (*p == '#' || *p == ';') {
                *p = '\0';
                break;
            }
        }

        char* trimmed = trim_whitespace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '[') {
            continue;
        }

        char* eq = strchr(trimmed, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        char* key = trim_whitespace(trimmed);
        char* val = trim_whitespace(eq + 1);

        if (strcmp(key, "username") == 0) {
            strncpy(g_config.username, val, sizeof(g_config.username) - 1);
            g_config.username[sizeof(g_config.username) - 1] = '\0';
        } else if (strcmp(key, "password") == 0) {
            strncpy(g_config.password, val, sizeof(g_config.password) - 1);
            g_config.password[sizeof(g_config.password) - 1] = '\0';
        } else if (strcmp(key, "proxy_ip") == 0) {
            if (val[0] != '\0') {
                strncpy(g_config.proxy_ip, val, sizeof(g_config.proxy_ip) - 1);
                g_config.proxy_ip[sizeof(g_config.proxy_ip) - 1] = '\0';
            }
        } else if (strcmp(key, "proxy_port") == 0) {
            int port = atoi(val);
            if (port > 0 && port <= 65535) {
                g_config.proxy_port = port;
            }
        } else if (strcmp(key, "wifi_profile") == 0) {
            int prof = atoi(val);
            if (prof >= 1 && prof <= 20) {
                g_config.wifi_profile = prof;
            }
        } else if (strcmp(key, "hardcore_mode") == 0) {
            g_config.hardcore_mode = (atoi(val) != 0) ? 1 : 0;
        } else if (strcmp(key, "video_scale") == 0) {
            int scale = atoi(val);
            if (scale >= 0 && scale <= 2) {
                g_config.video_scale = scale;
            }
        } else if (strcmp(key, "video_filter") == 0) {
            g_config.video_filter = (atoi(val) != 0) ? 1 : 0;
        }
    }

    fclose(fp);

    PSPRA_LOG("[Config] Loaded settings from %s (user: %s, proxy: %s:%d)\n",
              loaded_path,
              (g_config.username[0] != '\0') ? g_config.username : "<empty>",
              g_config.proxy_ip,
              g_config.proxy_port);

    if (!pspra_config_has_valid_credentials()) {
        PSPRA_LOG("[Config] Warning: Username or password is missing or set to placeholder in %s.\n",
                  loaded_path);
        return 0;
    }

    return 1;
}
