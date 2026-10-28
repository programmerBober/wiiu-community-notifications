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
    if (!dst || !src || max_len == 0) return;
    strncpy(dst, src, max_len - 1);
    dst[max_len - 1] = '\0';
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

static const struct {
    const char* tag;
    const char* sender;
    const char* title;
    const char* message;
    const char* app;
    const char* feeling;
    uint8_t priority;
} s_auto_catalog[] = {
    {
        "splatoon",
        "Squid Research Lab",
        "Inkopolis News Bulletin",
        "Incoming transmission from the Squid Sisters! Turf War rotation updated. Stay fresh on the battleground!",
        "0005000010176A00",
        "HAPPY",
        1
    },
    {
        "pretendo",
        "Pretendo Network",
        "Pretendo Network Status",
        "All network game services are online! Enjoy multiplayer matchmaking and Miiverse interactions across Cafe OS.",
        "0005001010040200",
        "HAPPY",
        1
    },
    {
        "mk8",
        "Mario Kart TV",
        "Mario Kart 8 Tournament",
        "Weekly 150cc Worldwide GP Tournament is now open! Hit the track and drift your way to victory!",
        "000500001010EC00",
        "SURPRISED",
        1
    },
    {
        "minecraft",
        "Minecraft News",
        "Battle Mini Game Lobby",
        "Minigame arenas are buzzing! Jump into Battle and Glide public lobbies with friends on Nintendo Network.",
        "00050000101D7500",
        "HAPPY",
        1
    },
    {
        "smm",
        "Course World",
        "Course World Spotlight",
        "New top-rated courses are trending! Fire up Course World and see if you can conquer the Super Expert gauntlet.",
        "000500001018DD00",
        "INSPIRED",
        1
    }
};

static uint32_t s_auto_index = 0;

bool Feed_GetNextAutonomous(CommunityNotification* out_item) {
    if (!out_item) return false;
    memset(out_item, 0, sizeof(CommunityNotification));

    uint32_t cat_size = sizeof(s_auto_catalog) / sizeof(s_auto_catalog[0]);
    uint32_t cur = s_auto_index % cat_size;
    s_auto_index++;

    time_t now = time(NULL);
    snprintf(out_item->id, sizeof(out_item->id), "auto_%s_%u_%u", s_auto_catalog[cur].tag, (uint32_t)now, s_auto_index);
    SafeCopyString(out_item->sender, s_auto_catalog[cur].sender, sizeof(out_item->sender));
    SafeCopyString(out_item->title, s_auto_catalog[cur].title, sizeof(out_item->title));
    SafeCopyString(out_item->message, s_auto_catalog[cur].message, sizeof(out_item->message));
    SafeCopyString(out_item->app, s_auto_catalog[cur].app, sizeof(out_item->app));
    SafeCopyString(out_item->feeling, s_auto_catalog[cur].feeling, sizeof(out_item->feeling));
    out_item->timestamp = (uint32_t)now;
    out_item->priority = s_auto_catalog[cur].priority;
    out_item->duration_sec = 6.0f;
    return true;
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
