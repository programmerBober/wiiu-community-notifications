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

typedef enum {
    NOTIFY_TYPE_GAME_EVENT    = 0, // Splatfests, tournaments, challenge seeds, boss raids
    NOTIFY_TYPE_MAINTENANCE   = 1, // Pretendo server status, downtime, security alerts
    NOTIFY_TYPE_COMMUNITY     = 2, // Juxtaposition/Miiverse drawings, plaza trends, forum news
    NOTIFY_TYPE_FRIEND_INVITE = 3, // Friend online, lobby open, game invitations
    NOTIFY_TYPE_ACHIEVEMENT   = 4, // High scores, course clear records, speedrun milestones
    NOTIFY_TYPE_SYSTEM_UPDATE = 5  // Aroma environment, plugin updates, firmware patches
} NotificationType;

typedef struct {
    char id[MAX_NOTIFY_ID_LEN];
    char sender[MAX_NOTIFY_TITLE_LEN];
    char title[MAX_NOTIFY_TITLE_LEN];
    char message[MAX_NOTIFY_TEXT_LEN];
    char app[32];
    char feeling[32];
    char type_str[24];
    NotificationType type;
    uint32_t timestamp;
    uint8_t priority;     // 0 = standard, 1 = normal, 2 = urgent/high
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
 * Retrieves the next autonomous community bulletin from the built-in catalog rotation.
 */
bool Feed_GetNextAutonomous(CommunityNotification* out_item);

/**
 * Reads feed from SD card storage directory.
 */
bool Feed_LoadFromSD(NotificationFeed* out_feed);

/**
 * Returns human-readable label for a notification type.
 */
const char* Feed_GetTypeString(NotificationType type);

/**
 * Parses type string into NotificationType enum.
 */
NotificationType Feed_ParseTypeString(const char* str);

/**
 * Cleans up feed subsystem.
 */
void Feed_Shutdown(void);

#ifdef __cplusplus
}
#endif
