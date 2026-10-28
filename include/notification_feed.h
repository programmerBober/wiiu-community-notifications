#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FEED_NOTIFICATIONS 32
#define MAX_NOTIFY_TEXT_LEN    256
#define MAX_NOTIFY_TITLE_LEN   64
#define MAX_NOTIFY_ID_LEN      64

typedef struct {
    char id[MAX_NOTIFY_ID_LEN];
    char sender[MAX_NOTIFY_TITLE_LEN];
    char title[MAX_NOTIFY_TITLE_LEN];
    char message[MAX_NOTIFY_TEXT_LEN];
    char app[32];
    char feeling[32];
    uint32_t timestamp;
    uint8_t priority;     // 0 = standard, 1 = high (triggers LED + sound)
    float duration_sec;
} CommunityNotification;

typedef struct {
    uint32_t version;
    uint32_t count;
    CommunityNotification items[MAX_FEED_NOTIFICATIONS];
} NotificationFeed;

/**
 * Initializes feed parser and loads last seen notification state.
 */
bool Feed_Init(const char* storage_path);

/**
 * Parses JSON buffer into a structured NotificationFeed.
 */
bool Feed_ParseJSON(const char* json_str, size_t length, NotificationFeed* out_feed);

/**
 * Checks if the notification ID was already seen/displayed.
 */
bool Feed_IsSeen(const char* id);

/**
 * Marks the notification ID as seen and updates persistent state on SD card.
 */
bool Feed_MarkSeen(const char* id);

/**
 * Appends a notification to the local history log on SD card.
 */
bool Feed_AppendHistory(const CommunityNotification* item);

/**
 * Reads feed from SD card storage directory.
 */
bool Feed_LoadFromSD(NotificationFeed* out_feed);

/**
 * Cleans up feed subsystem.
 */
void Feed_Shutdown(void);

#ifdef __cplusplus
}
#endif
