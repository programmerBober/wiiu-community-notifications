#include "notification_config.h"
#include <string.h>

void Config_SetDefaults(CommunityNotifyConfig* config) {
    if (!config) return;
    config->enable_led = true;
    config->enable_screen_overlay = true;
    config->enable_history_log = true;
    config->poll_interval_sec = NOTIFY_DEFAULT_POLL_INTERVAL_SEC;
    strncpy(config->feed_url, "http://wiiu-community.local/api/feed.json", sizeof(config->feed_url));
}
