#include "notification_config.h"
#include <string.h>

void Config_SetDefaults(CommunityNotifyConfig* config) {
    if (!config) return;
    config->enable_led = true;
    config->enable_screen_overlay = false;
    config->enable_history_log = true;
    config->enable_autonomous = true;
    config->enable_pretendo_sync = true;
    config->enable_tcp = true;
    config->poll_interval_sec = NOTIFY_DEFAULT_POLL_INTERVAL_SEC;
    config->led_duration_sec = 10; // Default: 10s breathing pulse before auto-off
    strncpy(config->feed_url, "http://wiiu-community.local/api/feed.json", sizeof(config->feed_url));
}
