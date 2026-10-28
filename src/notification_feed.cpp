#include "notification_feed.h"
#include "notification_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#if defined(__WIIU__) || defined(__WUT__)
#include <coreinit/filesystem.h>
#endif

static char s_storage_dir[256] = {0};
static char s_seen_ids[128][MAX_NOTIFY_ID_LEN];
static uint32_t s_seen_count = 0;
static bool s_feed_initialized = false;

static void EnsureStorageDir(const char* dir) {
#if !defined(__WIIU__) && !defined(__WUT__)
    mkdir(dir, 0755);
#endif
}

static void SafeCopyString(char* dst, const char* src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= max_len) len = max_len - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static const char* SkipWhitespace(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static const char* ExtractJsonStringValue(const char* key, const char* json_obj, char* out_val, size_t max_len) {
    out_val[0] = '\0';
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char* key_pos = strstr(json_obj, search_pattern);
    if (!key_pos) return nullptr;

    const char* colon = strchr(key_pos + strlen(search_pattern), ':');
    if (!colon) return nullptr;

    const char* val_start = SkipWhitespace(colon + 1);
    if (*val_start != '\"') return nullptr;
    val_start++; // skip opening quote

    size_t idx = 0;
    while (*val_start && *val_start != '\"' && idx < max_len - 1) {
        if (*val_start == '\\' && *(val_start + 1)) {
            val_start++;
            if (*val_start == 'n') out_val[idx++] = '\n';
            else if (*val_start == '\"') out_val[idx++] = '\"';
            else if (*val_start == '\\') out_val[idx++] = '\\';
            else out_val[idx++] = *val_start;
        } else {
            out_val[idx++] = *val_start;
        }
        val_start++;
    }
    out_val[idx] = '\0';
    return val_start;
}

static double ExtractJsonNumberValue(const char* key, const char* json_obj, double default_val) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char* key_pos = strstr(json_obj, search_pattern);
    if (!key_pos) return default_val;

    const char* colon = strchr(key_pos + strlen(search_pattern), ':');
    if (!colon) return default_val;

    const char* val_start = SkipWhitespace(colon + 1);
    return strtod(val_start, nullptr);
}

bool Feed_Init(const char* storage_path) {
    if (storage_path && strlen(storage_path) > 0) {
        SafeCopyString(s_storage_dir, storage_path, sizeof(s_storage_dir));
    } else {
        SafeCopyString(s_storage_dir, NOTIFY_SD_DIR, sizeof(s_storage_dir));
    }

    EnsureStorageDir(s_storage_dir);

    // Load seen IDs from persistent file
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_storage_dir, NOTIFY_STATE_FILE);

    s_seen_count = 0;
    FILE* f = fopen(path, "r");
    if (f) {
        char line[MAX_NOTIFY_ID_LEN];
        while (fgets(line, sizeof(line), f) && s_seen_count < 128) {
            // Trim newline
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
                line[--len] = '\0';
            }
            if (len > 0) {
                SafeCopyString(s_seen_ids[s_seen_count++], line, MAX_NOTIFY_ID_LEN);
            }
        }
        fclose(f);
    }

    s_feed_initialized = true;
    return true;
}

bool Feed_ParseJSON(const char* json_str, size_t length, NotificationFeed* out_feed) {
    (void)length;
    if (!json_str || !out_feed) return false;

    memset(out_feed, 0, sizeof(NotificationFeed));
    out_feed->version = (uint32_t)ExtractJsonNumberValue("version", json_str, 1.0);

    const char* array_start = strstr(json_str, "\"notifications\"");
    if (!array_start) return false;

    array_start = strchr(array_start, '[');
    if (!array_start) return false;

    const char* cursor = array_start + 1;

    while (*cursor && out_feed->count < MAX_FEED_NOTIFICATIONS) {
        cursor = SkipWhitespace(cursor);
        if (*cursor == ']') break;
        if (*cursor == ',') { cursor++; continue; }
        if (*cursor != '{') { cursor++; continue; }

        // Find matching closing brace for this notification object
        const char* obj_start = cursor;
        int depth = 0;
        const char* obj_end = nullptr;

        while (*cursor) {
            if (*cursor == '{') depth++;
            else if (*cursor == '}') {
                depth--;
                if (depth == 0) {
                    obj_end = cursor;
                    break;
                }
            }
            cursor++;
        }

        if (!obj_end) break;

        size_t obj_len = (size_t)(obj_end - obj_start + 1);
        char obj_buf[1024];
        if (obj_len >= sizeof(obj_buf)) obj_len = sizeof(obj_buf) - 1;
        memcpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = '\0';

        CommunityNotification* item = &out_feed->items[out_feed->count];
        ExtractJsonStringValue("id", obj_buf, item->id, sizeof(item->id));
        ExtractJsonStringValue("sender", obj_buf, item->sender, sizeof(item->sender));
        ExtractJsonStringValue("title", obj_buf, item->title, sizeof(item->title));
        ExtractJsonStringValue("message", obj_buf, item->message, sizeof(item->message));
        ExtractJsonStringValue("app", obj_buf, item->app, sizeof(item->app));
        ExtractJsonStringValue("feeling", obj_buf, item->feeling, sizeof(item->feeling));
        ExtractJsonStringValue("type", obj_buf, item->type_str, sizeof(item->type_str));
        if (item->type_str[0] != '\0') {
            item->type = Feed_ParseTypeString(item->type_str);
        } else {
            item->type = NOTIFY_TYPE_GAME_EVENT;
            SafeCopyString(item->type_str, Feed_GetTypeString(item->type), sizeof(item->type_str));
        }
        if (item->feeling[0] == '\0') {
            SafeCopyString(item->feeling, "HAPPY", sizeof(item->feeling));
        }
        if (item->app[0] == '\0') {
            if (strstr(item->title, "Splatoon") || strstr(item->title, "Splatfest") || strstr(item->message, "Inkopolis")) {
                SafeCopyString(item->app, "0005000010176A00", sizeof(item->app));
            } else if (strstr(item->title, "Minecraft")) {
                SafeCopyString(item->app, "00050000101D7500", sizeof(item->app));
            } else if (strstr(item->title, "Kart 8") || strstr(item->sender, "Kart")) {
                SafeCopyString(item->app, "000500001010EC00", sizeof(item->app));
            } else if (strstr(item->sender, "Mario Maker") || strstr(item->title, "Course")) {
                SafeCopyString(item->app, "000500001018DD00", sizeof(item->app));
            }
        }
        item->timestamp = (uint32_t)ExtractJsonNumberValue("timestamp", obj_buf, 0);
        item->priority = (uint8_t)ExtractJsonNumberValue("priority", obj_buf, 0);
        item->duration_sec = (float)ExtractJsonNumberValue("duration", obj_buf, 6.0);

        if (strlen(item->id) > 0 && strlen(item->message) > 0) {
            out_feed->count++;
        }

        cursor = obj_end + 1;
    }

    return true;
}

bool Feed_IsSeen(const char* id) {
    if (!id || strlen(id) == 0) return true;
    for (uint32_t i = 0; i < s_seen_count; i++) {
        if (strcmp(s_seen_ids[i], id) == 0) {
            return true;
        }
    }
    return false;
}

bool Feed_MarkSeen(const char* id) {
    if (!id || strlen(id) == 0) return false;
    if (Feed_IsSeen(id)) return true;

    if (s_seen_count < 128) {
        SafeCopyString(s_seen_ids[s_seen_count++], id, MAX_NOTIFY_ID_LEN);
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_storage_dir, NOTIFY_STATE_FILE);
    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s\n", id);
        fclose(f);
    }
    return true;
}

bool Feed_AppendHistory(const CommunityNotification* item) {
    if (!item) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_storage_dir, NOTIFY_HISTORY_FILE);
    FILE* f = fopen(path, "a");
    if (!f) return false;

    fprintf(f, "[%u] [%s] %s: %s\n", 
            item->timestamp, 
            item->sender[0] ? item->sender : "Community",
            item->title[0] ? item->title : "Notice",
            item->message);
    fclose(f);
    return true;
}

const char* Feed_GetTypeString(NotificationType type) {
    switch (type) {
        case NOTIFY_TYPE_GAME_EVENT:    return "game";
        case NOTIFY_TYPE_MAINTENANCE:   return "maintenance";
        case NOTIFY_TYPE_COMMUNITY:     return "community";
        case NOTIFY_TYPE_FRIEND_INVITE: return "friend";
        case NOTIFY_TYPE_ACHIEVEMENT:   return "achievement";
        case NOTIFY_TYPE_SYSTEM_UPDATE: return "system";
        default:                        return "game";
    }
}

NotificationType Feed_ParseTypeString(const char* str) {
    if (!str || str[0] == '\0') return NOTIFY_TYPE_GAME_EVENT;
    if (strcasecmp(str, "maintenance") == 0 || strcasecmp(str, "alert") == 0 || strcasecmp(str, "outage") == 0) {
        return NOTIFY_TYPE_MAINTENANCE;
    }
    if (strcasecmp(str, "community") == 0 || strcasecmp(str, "miiverse") == 0 || strcasecmp(str, "juxtaposition") == 0) {
        return NOTIFY_TYPE_COMMUNITY;
    }
    if (strcasecmp(str, "friend") == 0 || strcasecmp(str, "invite") == 0 || strcasecmp(str, "party") == 0) {
        return NOTIFY_TYPE_FRIEND_INVITE;
    }
    if (strcasecmp(str, "achievement") == 0 || strcasecmp(str, "record") == 0 || strcasecmp(str, "milestone") == 0) {
        return NOTIFY_TYPE_ACHIEVEMENT;
    }
    if (strcasecmp(str, "system") == 0 || strcasecmp(str, "update") == 0 || strcasecmp(str, "aroma") == 0) {
        return NOTIFY_TYPE_SYSTEM_UPDATE;
    }
    return NOTIFY_TYPE_GAME_EVENT;
}

static const struct {
    const char* tag;
    NotificationType type;
    const char* type_str;
    const char* sender;
    const char* title;
    const char* message;
    const char* app;
    const char* feeling;
    uint8_t priority;
} s_auto_catalog[] = {
    {
        "splatoon",
        NOTIFY_TYPE_GAME_EVENT,
        "game",
        "Squid Research Lab",
        "Splatoon - Inkopolis News",
        "Splatfest announcement: Team Cats vs Team Dogs is live! Head to Inkopolis Plaza to claim your tee!",
        "0005000010176A00",
        "HAPPY",
        1
    },
    {
        "mariokart8",
        NOTIFY_TYPE_GAME_EVENT,
        "game",
        "Mario Kart TV",
        "150cc Mushroom Cup Tournament",
        "Pretendo Weekly Grand Prix is open! Hit the track with 150cc karts and set your best time trial records!",
        "0005000010149100",
        "HAPPY",
        1
    },
    {
        "smash",
        NOTIFY_TYPE_GAME_EVENT,
        "game",
        "Smash Bros. Arena",
        "For Glory - 1-on-1 Tournament",
        "Challengers approaching! Online For Glory bracket has commenced on Final Destination. Settle it in Smash!",
        "00050000101C6300",
        "SURPRISED",
        1
    },
    {
        "smm_course",
        NOTIFY_TYPE_ACHIEVEMENT,
        "achievement",
        "Course World",
        "Trending Course: Bowsers Keep",
        "A new Expert course by @BuilderMario is trending in the Course World with 450+ clears! Jump in and try it!",
        "00050000101B4300",
        "LIKE",
        1
    },
    {
        "zelda_tingle",
        NOTIFY_TYPE_COMMUNITY,
        "community",
        "Tingle Bottle",
        "Letter from the Great Sea",
        "You washed a Tingle Bottle ashore containing a sea photograph and Miiverse message from a fellow voyager!",
        "0005000010142700",
        "LIKE",
        1
    },
    {
        "nsmbu",
        NOTIFY_TYPE_GAME_EVENT,
        "game",
        "Toad House",
        "Coin Battle Challenge",
        "Boost Mode challenges and 4-player Coin Battle records are open across Acorn Plains! Grab your GamePad!",
        "0005000010105700",
        "HAPPY",
        1
    },
    {
        "minecraft",
        NOTIFY_TYPE_FRIEND_INVITE,
        "friend",
        "Minecraft News",
        "Battle Mini Game - Arena Open!",
        "Join your friends on Pretendo Network in the Medieval arena! Battle and Glide lobbies are open.",
        "00050000101D7500",
        "HAPPY",
        1
    },
    {
        "fast_racing",
        NOTIFY_TYPE_ACHIEVEMENT,
        "achievement",
        "FAST Racing NEO",
        "Subsonic League Cup Open",
        "Switch your phase colors, hit supersonic boost pads, and set a new lap record on the tracks!",
        "00050000101D6000",
        "INSPIRED",
        1
    },
    {
        "starfox",
        NOTIFY_TYPE_GAME_EVENT,
        "game",
        "Star Fox Zero",
        "Corneria Defense Alert",
        "General Pepper to Star Fox: Enemy forces detected near Sector Beta! Scramble your Arwings immediately!",
        "00050000101B0500",
        "SURPRISED",
        1
    },
    {
        "isaac",
        NOTIFY_TYPE_GAME_EVENT,
        "game",
        "The Binding of Isaac",
        "Daily Seeded Run Available",
        "Today's seeded challenge is live! Descend into the Basement and fight for the top of the leaderboards!",
        "00050000101C9B00",
        "SURPRISED",
        1
    },
    {
        "pretendo_community",
        NOTIFY_TYPE_COMMUNITY,
        "community",
        "Juxtaposition",
        "Trending Miiverse Post",
        "Your favorite community has new drawings and game tips! Open Juxtaposition to give Yeahs and share replies.",
        "0005001010040200",
        "HAPPY",
        1
    },
    {
        "pretendo_maintenance",
        NOTIFY_TYPE_MAINTENANCE,
        "maintenance",
        "Pretendo Network",
        "Online Services Status: OK",
        "All game matchmaking servers, Juxtaposition, and account authentication systems are operating normally.",
        "000500101004D200",
        "HAPPY",
        1
    }
};

static uint32_t s_auto_index = 0;

bool Feed_GetNextAutonomous(CommunityNotification* out_item) {
    if (!out_item) return false;
    memset(out_item, 0, sizeof(CommunityNotification));

    uint32_t cat_size = sizeof(s_auto_catalog) / sizeof(s_auto_catalog[0]);
    for (uint32_t i = 0; i < cat_size; i++) {
        uint32_t cur = (s_auto_index + i) % cat_size;
        char candidate_id[MAX_NOTIFY_ID_LEN];
        snprintf(candidate_id, sizeof(candidate_id), "auto_%s", s_auto_catalog[cur].tag);
        if (!Feed_IsSeen(candidate_id)) {
            s_auto_index = (cur + 1) % cat_size;
            SafeCopyString(out_item->id, candidate_id, sizeof(out_item->id));
            SafeCopyString(out_item->sender, s_auto_catalog[cur].sender, sizeof(out_item->sender));
            SafeCopyString(out_item->title, s_auto_catalog[cur].title, sizeof(out_item->title));
            SafeCopyString(out_item->message, s_auto_catalog[cur].message, sizeof(out_item->message));
            SafeCopyString(out_item->app, s_auto_catalog[cur].app, sizeof(out_item->app));
            SafeCopyString(out_item->feeling, s_auto_catalog[cur].feeling, sizeof(out_item->feeling));
            out_item->type = s_auto_catalog[cur].type;
            SafeCopyString(out_item->type_str, s_auto_catalog[cur].type_str, sizeof(out_item->type_str));
            out_item->timestamp = (uint32_t)time(NULL);
            out_item->priority = s_auto_catalog[cur].priority;
            out_item->duration_sec = 6.0f;
            return true;
        }
    }
    return false;
}

bool Feed_LoadFromSD(NotificationFeed* out_feed) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_storage_dir, NOTIFY_FEED_FILE);

    FILE* f = fopen(path, "rb");
    if (!f) {
        // Fallback relative or alternative path
        snprintf(path, sizeof(path), "./%s", NOTIFY_FEED_FILE);
        f = fopen(path, "rb");
        if (!f) return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 64 * 1024) {
        fclose(f);
        return false;
    }

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buffer, 1, size, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    bool res = Feed_ParseJSON(buffer, read_bytes, out_feed);
    free(buffer);
    return res;
}

void Feed_Shutdown(void) {
    s_feed_initialized = false;
    s_seen_count = 0;
}
