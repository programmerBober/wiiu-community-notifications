#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIIU_NOTIFY_VERSION_STR "v0.1.0-alpha"

#define NOTIFY_SD_DIR           "fs:/vol/external01/wiiu/notifications"
#define NOTIFY_FALLBACK_SD_DIR  "/wiiu/notifications"
#define NOTIFY_FEED_FILE        "feed.json"
#define NOTIFY_HISTORY_FILE     "history.log"
#define NOTIFY_STATE_FILE       "last_seen.txt"

#define NOTIFY_DEFAULT_POLL_INTERVAL_SEC 300 // 5 minutes
#define NOTIFY_BOOT_DELAY_SEC            3   // Wait 3s after title start

// Authentic Wii U SpotPass Theme Colors (RGBA)
#define WIIU_BLUE_R  0
#define WIIU_BLUE_G  162
#define WIIU_BLUE_B  232
#define WIIU_BLUE_A  240

#define WIIU_DARK_R  20
#define WIIU_DARK_G  30
#define WIIU_DARK_B  44
#define WIIU_DARK_A  245

#define WIIU_TEXT_R  255
#define WIIU_TEXT_G  255
#define WIIU_TEXT_B  255
#define WIIU_TEXT_A  255

typedef struct {
    bool enable_led;
    bool enable_screen_overlay;
    bool enable_history_log;
    bool enable_autonomous;
    bool enable_pretendo_sync;
    bool enable_tcp;
    uint32_t poll_interval_sec;
    char feed_url[256];
} CommunityNotifyConfig;

void Config_SetDefaults(CommunityNotifyConfig* config);

#ifdef __cplusplus
}
#endif
