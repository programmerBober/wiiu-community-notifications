#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#include "notification_config.h"
#include "notification_feed.h"
#include "drc_led_controller.h"
#include "notifications_overlay.h"
#include "news_store.h"

static CommunityNotifyConfig s_config;
static volatile bool s_plugin_running = false;

static void EscapeXmlString(const char* src, char* dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 6 < dst_sz; i++) {
        char c = src[i];
        if (c == '&') {
            memcpy(dst + out, "&amp;", 5);
            out += 5;
        } else if (c == '<') {
            memcpy(dst + out, "&lt;", 4);
            out += 4;
        } else if (c == '>') {
            memcpy(dst + out, "&gt;", 4);
            out += 4;
        } else if (c == '\"') {
            memcpy(dst + out, "&quot;", 6);
            out += 6;
        } else if (c == '\'') {
            memcpy(dst + out, "&apos;", 6);
            out += 6;
        } else {
            dst[out++] = c;
        }
    }
    dst[out] = '\0';
}

static void FormatSpotPassXml(char* dst, size_t dst_sz,
                             const char* title, const char* body,
                             const char* title_id, const char* feeling) {
    char safe_title[256];
    char safe_body[768];
    EscapeXmlString(title ? title : "Notice", safe_title, sizeof(safe_title));
    EscapeXmlString(body ? body : "", safe_body, sizeof(safe_body));

    const char* safe_feeling = (feeling && feeling[0]) ? feeling : "HAPPY";
    bool has_game_app = (title_id && strlen(title_id) == 16 &&
                         strcmp(title_id, "000500101004D200") != 0 &&
                         strcmp(title_id, "000500101004d200") != 0);

    if (has_game_app) {
        snprintf(dst, dst_sz,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<Message>\n"
            "  <Subject>%s</Subject>\n"
            "  <Body>%s</Body>\n"
            "  <TitleId>%s</TitleId>\n"
            "  <HighPriority>true</HighPriority>\n"
            "  <ForceNotify>true</ForceNotify>\n"
            "  <Feeling>%s</Feeling>\n"
            "  <OptoutType>NEVER</OptoutType>\n"
            "  <LinkType>APP</LinkType>\n"
            "  <Links>\n"
            "    <AppLink>\n"
            "      <TitleId>%s</TitleId>\n"
            "      <Parameter></Parameter>\n"
            "    </AppLink>\n"
            "  </Links>\n"
            "</Message>\n",
            safe_title, safe_body, title_id, safe_feeling, title_id);
    } else {
        snprintf(dst, dst_sz,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<Message>\n"
            "  <Subject>%s</Subject>\n"
            "  <Body>%s</Body>\n"
            "  <TitleId>0005001010040200</TitleId>\n"
            "  <HighPriority>true</HighPriority>\n"
            "  <ForceNotify>true</ForceNotify>\n"
            "  <Feeling>%s</Feeling>\n"
            "  <OptoutType>NEVER</OptoutType>\n"
            "  <LinkType>NONE</LinkType>\n"
            "</Message>\n",
            safe_title, safe_body, safe_feeling);
    }
}

#if defined(__WIIU__) || defined(__WUT__) || defined(__WUPS__)
#include <wups.h>
#include <wups/config_api.h>
#include <wups/storage.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <malloc.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <coreinit/dynload.h>
#include <coreinit/debug.h>
#include <nn/act/client_cpp.h>
#include <vpad/input.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

WUPS_PLUGIN_NAME("CommunityNotifications");
WUPS_PLUGIN_DESCRIPTION("Native Wii U SpotPass & Pretendo Notification Injector");
WUPS_PLUGIN_VERSION(WIIU_NOTIFY_VERSION_STR);
WUPS_PLUGIN_AUTHOR("programmerBober");
WUPS_PLUGIN_LICENSE("GPLv3");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("community_notifications");

#define PLUGIN_TCP_PORT 7331
#define PLUGIN_LOG_SD   "fs:/vol/external01/wiiu/notifications/post_log.txt"

static void ClearArrivalFlags();

static OSThread s_worker_thread;
static uint8_t s_thread_stack[0x8000] __attribute__((aligned(0x20)));
static bool s_thread_active = false;
static int s_server_fd = -1;
static uint32_t s_led_active_ticks = 0;

static void PluginLog(const char* fmt, ...) {
    FILE* f = fopen(PLUGIN_LOG_SD, "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}


// Raise the "new notification" badge / HOME-menu glow. These are tiny IOSU-managed
// state flags (no news payload ever touches MLC; payloads live on SD via news_store).
static void SignalNewArrival() {
    OSDynLoad_Module boss_mod;
    if (OSDynLoad_Acquire("nn_boss.rpl", &boss_mod) != OS_DYNLOAD_OK) return;

    uint32_t (*boss_init)() = nullptr;
    uint32_t (*boss_finalize)() = nullptr;
    uint32_t (*boss_set_new_arrival)(const uint32_t *accountThis, uint32_t flag) = nullptr;
    typedef void (*boss_almighty_ctor_t)(void* self, uint32_t pid, const uint64_t* title_id);
    typedef void (*boss_almighty_dtor_t)(void* self);
    typedef void (*boss_almighty_set_flag_t)(void* self, bool flag);
    boss_almighty_ctor_t boss_almighty_ctor = nullptr;
    boss_almighty_dtor_t boss_almighty_dtor = nullptr;
    boss_almighty_set_flag_t boss_almighty_set_mark = nullptr;
    boss_almighty_set_flag_t boss_almighty_set_arrival = nullptr;

    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Initialize__Q2_2nn4bossFv", (void**)&boss_init);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Finalize__Q2_2nn4bossFv", (void**)&boss_finalize);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss7AccountFb", (void**)&boss_set_new_arrival);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__ct__Q3_2nn4boss13AlmightyTitleFUiQ3_2nn4boss7TitleID", (void**)&boss_almighty_ctor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__dt__Q3_2nn4boss13AlmightyTitleFv", (void**)&boss_almighty_dtor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalMarkFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_mark);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_arrival);
    if (!boss_init) return;

    nn::act::Initialize();
    uint32_t target_pid = nn::act::GetPersistentId();
    for (uint8_t s = 1; s <= 12 && target_pid == 0; s++) {
        target_pid = nn::act::GetPersistentIdEx(s);
    }
    if (target_pid == 0) target_pid = 0x80000001;

    boss_init();
    if (boss_set_new_arrival) {
        boss_set_new_arrival(&target_pid, 1);
    }
    if (boss_almighty_ctor && boss_almighty_set_mark) {
        uint8_t almighty_buf[32] __attribute__((aligned(8)));
        memset(almighty_buf, 0, sizeof(almighty_buf));
        uint64_t notif_title_id = 0x000500101004D200ULL;
        boss_almighty_ctor(almighty_buf, target_pid, &notif_title_id);
        boss_almighty_set_mark(almighty_buf, 1);
        if (boss_almighty_set_arrival) boss_almighty_set_arrival(almighty_buf, 1);
        if (boss_almighty_dtor) boss_almighty_dtor(almighty_buf);
    }
    if (boss_finalize) boss_finalize();
    nn::act::Finalize();
    PluginLog("[Plugin] New-arrival flags raised for PID 0x%08X", target_pid);
}

// Compress the SpotPass XML and store it on the SD card. The official Notification
// applet picks it up through the nn_boss hooks in boss_hooks.cpp. Zero MLC writes.
static bool PostOfficialNotification(const char* xml_text, char* out_status, size_t out_status_sz) {
    OSDynLoad_Module zlib_mod;
    if (OSDynLoad_Acquire("zlib125.rpl", &zlib_mod) != OS_DYNLOAD_OK) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: zlib125.rpl missing");
        PluginLog("[Plugin] Err: zlib125.rpl missing");
        return false;
    }
    int (*z_compress)(uint8_t *dest, uint32_t *destLen, const uint8_t *source, uint32_t sourceLen) = nullptr;
    OSDynLoad_FindExport(zlib_mod, OS_DYNLOAD_EXPORT_FUNC, "compress", (void**)&z_compress);
    if (!z_compress) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: zlib compress not found");
        return false;
    }

    // Always compress XML payload with zlib (required by inf.rpx decompressor)
    uint32_t xml_len = strlen(xml_text);
    uint32_t comp_len = xml_len + 512;
    uint8_t* comp_buf = (uint8_t*)memalign(0x40, comp_len);
    if (!comp_buf) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: OOM");
        return false;
    }
    int z_ret = z_compress(comp_buf, &comp_len, (const uint8_t*)xml_text, xml_len);
    if (z_ret != 0) {
        free(comp_buf);
        if (out_status) snprintf(out_status, out_status_sz, "Err: zlib %d", z_ret);
        return false;
    }

    uint32_t serial = 0;
    bool ok = NewsStore_Save(comp_buf, comp_len, &serial);
    free(comp_buf);
    if (!ok) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: SD write failed");
        PluginLog("[Plugin] Err: could not save news to SD (" NOTIFY_SD_DIR "/news)");
        return false;
    }
    PluginLog("[Plugin] SD news saved: serial=0x%08X len=%u", serial, comp_len);

    SignalNewArrival();
    if (out_status) snprintf(out_status, out_status_sz, "OK: sd=0x%08X", serial);
    return true;
}

static void HandleTcpClient(int client_fd) {
    char buf[2048];
    memset(buf, 0, sizeof(buf));
    int bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (bytes <= 0) return;
    buf[bytes] = '\0';

    char title[128] = "Desktop Notification";
    char body[512] = "New notification received.";
    char app[32] = "";
    char feeling[32] = "HAPPY";
    char type_str[32] = "game";

    // Fast-path commands
    if (strstr(buf, "\"cmd\":\"led_off\"") || strstr(buf, "\"cmd\": \"led_off\"")) {
        DrcLed_TurnOff();
        ClearArrivalFlags();
        s_led_active_ticks = 0;
        const char* rep = "OK: LED turned off and arrival flags cleared\n";
        send(client_fd, rep, strlen(rep), 0);
        return;
    }
    if (strstr(buf, "\"cmd\":\"led_on\"") || strstr(buf, "\"cmd\": \"led_on\"")) {
        DrcLed_TurnOn(1);
        s_led_active_ticks = (s_config.led_duration_sec > 0 ? s_config.led_duration_sec : 10) * 4;
        const char* rep = "OK: LED turned on (pattern 1)\n";
        send(client_fd, rep, strlen(rep), 0);
        return;
    }
    if (strstr(buf, "\"cmd\":\"clear_news\"") || strstr(buf, "\"cmd\": \"clear_news\"")) {
        char rep[96];
        snprintf(rep, sizeof(rep), "OK: removed %u SD notification(s)\n", NewsStore_ClearAll());
        send(client_fd, rep, strlen(rep), 0);
        return;
    }
    if (strstr(buf, "\"cmd\":\"clear\"") || strstr(buf, "\"cmd\": \"clear\"")) {
        ClearArrivalFlags();
        DrcLed_TurnOff();
        s_led_active_ticks = 0;
        const char* rep = "OK: Arrival flags cleared\n";
        send(client_fd, rep, strlen(rep), 0);
        return;
    }

    char* t_pos = strstr(buf, "\"title\":");
    if (t_pos) {
        char* q1 = strchr(t_pos + 8, '\"');
        if (q1) {
            char* q2 = strchr(q1 + 1, '\"');
            if (q2) {
                size_t l = (size_t)(q2 - (q1 + 1));
                if (l > sizeof(title) - 1) l = sizeof(title) - 1;
                strncpy(title, q1 + 1, l);
                title[l] = '\0';
            }
        }
    }

    char* b_pos = strstr(buf, "\"body\":");
    if (b_pos) {
        char* q1 = strchr(b_pos + 7, '\"');
        if (q1) {
            char* q2 = strchr(q1 + 1, '\"');
            if (q2) {
                size_t l = (size_t)(q2 - (q1 + 1));
                if (l > sizeof(body) - 1) l = sizeof(body) - 1;
                strncpy(body, q1 + 1, l);
                body[l] = '\0';
            }
        }
    }

    char* a_pos = strstr(buf, "\"app\":");
    if (a_pos) {
        char* q1 = strchr(a_pos + 6, '\"');
        if (q1) {
            char* q2 = strchr(q1 + 1, '\"');
            if (q2) {
                size_t l = (size_t)(q2 - (q1 + 1));
                if (l > sizeof(app) - 1) l = sizeof(app) - 1;
                strncpy(app, q1 + 1, l);
                app[l] = '\0';
            }
        }
    }

    char* f_pos = strstr(buf, "\"feeling\":");
    if (f_pos) {
        char* q1 = strchr(f_pos + 10, '\"');
        if (q1) {
            char* q2 = strchr(q1 + 1, '\"');
            if (q2) {
                size_t l = (size_t)(q2 - (q1 + 1));
                if (l > sizeof(feeling) - 1) l = sizeof(feeling) - 1;
                strncpy(feeling, q1 + 1, l);
                feeling[l] = '\0';
            }
        }
    }

    char* tp_pos = strstr(buf, "\"type\":");
    if (tp_pos) {
        char* q1 = strchr(tp_pos + 7, '\"');
        if (q1) {
            char* q2 = strchr(q1 + 1, '\"');
            if (q2) {
                size_t l = (size_t)(q2 - (q1 + 1));
                if (l > sizeof(type_str) - 1) l = sizeof(type_str) - 1;
                strncpy(type_str, q1 + 1, l);
                type_str[l] = '\0';
            }
        }
    }
    NotificationType ntype = Feed_ParseTypeString(type_str);

    char xml[1536];
    if (strncmp(buf, "<?xml", 5) == 0) {
        // Raw XML passthrough (debugging custom message layouts from the PC)
        strncpy(xml, buf, sizeof(xml) - 1);
        xml[sizeof(xml) - 1] = '\0';
    } else {
        FormatSpotPassXml(xml, sizeof(xml), title, body, app, feeling);
    }

    char status[128];
    bool ok = PostOfficialNotification(xml, status, sizeof(status));
    if (ok && s_config.enable_led) {
        DrcLed_TurnOn(1);
        s_led_active_ticks = (s_config.led_duration_sec > 0 ? s_config.led_duration_sec : 10) * 4;
    }

    if (ok && s_config.enable_screen_overlay) {
        NotificationStyle style = NOTIFY_STYLE_SPOTPASS_BLUE;
        if (ntype == NOTIFY_TYPE_MAINTENANCE) style = NOTIFY_STYLE_CRITICAL_RED;
        else if (ntype == NOTIFY_TYPE_COMMUNITY) style = NOTIFY_STYLE_DARK_ELEGANT;
        else if (ntype == NOTIFY_TYPE_FRIEND_INVITE) style = NOTIFY_STYLE_PARTY_PURPLE;
        else if (ntype == NOTIFY_TYPE_ACHIEVEMENT) style = NOTIFY_STYLE_SUCCESS_GREEN;
        else if (ntype == NOTIFY_TYPE_SYSTEM_UPDATE) style = NOTIFY_STYLE_WARNING_AMBER;
        Overlay_ShowToast(title, body, 7.0f, style);
    }

    char reply[160];
    snprintf(reply, sizeof(reply), "%s: %s\n", ok ? "OK" : "ERR", status);
    send(client_fd, reply, strlen(reply), 0);
}

static void OnAutonomousChanged(ConfigItemBoolean *item, bool newValue) {
    s_config.enable_autonomous = newValue;
    WUPSStorageAPI_StoreBool(nullptr, "enable_autonomous", newValue);
    PluginLog("[Plugin] Config: enable_autonomous = %d", (int)newValue);
}

static void OnLedChanged(ConfigItemBoolean *item, bool newValue) {
    s_config.enable_led = newValue;
    if (!newValue) {
        DrcLed_TurnOff();
        s_led_active_ticks = 0;
    }
    WUPSStorageAPI_StoreBool(nullptr, "enable_led", newValue);
    PluginLog("[Plugin] Config: enable_led = %d", (int)newValue);
}

static void OnLedDurationChanged(ConfigItemMultipleValues *item, uint32_t val) {
    s_config.led_duration_sec = val;
    WUPSStorageAPI_StoreU32(nullptr, "led_duration_sec", val);
    PluginLog("[Plugin] Config: led_duration_sec = %u", val);
}

static const ConfigItemMultipleValuesPair s_led_duration_pairs[] = {
    { 5, "5 seconds" },
    { 10, "10 seconds (SpotPass Default)" },
    { 30, "30 seconds" },
    { 60, "1 minute" }
};

static void OnOverlayChanged(ConfigItemBoolean *item, bool newValue) {
    s_config.enable_screen_overlay = newValue;
    WUPSStorageAPI_StoreBool(nullptr, "enable_screen_overlay", newValue);
    PluginLog("[Plugin] Config: enable_screen_overlay = %d", (int)newValue);
}

static void OnTcpChanged(ConfigItemBoolean *item, bool newValue) {
    s_config.enable_tcp = newValue;
    WUPSStorageAPI_StoreBool(nullptr, "enable_tcp", newValue);
    PluginLog("[Plugin] Config: enable_tcp = %d", (int)newValue);
}

static void OnIntervalChanged(ConfigItemMultipleValues *item, uint32_t val) {
    s_config.poll_interval_sec = val;
    WUPSStorageAPI_StoreU32(nullptr, "poll_interval_sec", val);
    PluginLog("[Plugin] Config: poll_interval_sec = %u", val);
}

static const ConfigItemMultipleValuesPair s_interval_pairs[] = {
    { 30, "30 seconds (Active Test)" },
    { 60, "1 minute" },
    { 300, "5 minutes (SpotPass Default)" },
    { 900, "15 minutes" },
    { 1800, "30 minutes" },
    { 3600, "1 hour" }
};

static WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle root) {
    WUPSConfigItemBoolean_AddToCategory(root, "enable_autonomous", "Autonomous Delivery (Zero-PC)", true, s_config.enable_autonomous, OnAutonomousChanged);
    WUPSConfigItemBoolean_AddToCategory(root, "enable_led", "GamePad Notification LED", true, s_config.enable_led, OnLedChanged);
    WUPSConfigItemBoolean_AddToCategory(root, "enable_overlay", "On-Screen Toast Overlay", false, s_config.enable_screen_overlay, OnOverlayChanged);
    WUPSConfigItemBoolean_AddToCategory(root, "enable_tcp", "PC Network Receiver (Port 7331)", true, s_config.enable_tcp, OnTcpChanged);

    int cur_idx = 2; // 5 minutes default
    for (int i = 0; i < (int)(sizeof(s_interval_pairs) / sizeof(s_interval_pairs[0])); i++) {
        if (s_interval_pairs[i].value == s_config.poll_interval_sec) {
            cur_idx = i;
            break;
        }
    }

    WUPSConfigItemMultipleValues_AddToCategory(
        root,
        "poll_interval",
        "Delivery / Check Interval",
        2, // default 5m
        cur_idx,
        (ConfigItemMultipleValuesPair*)s_interval_pairs,
        sizeof(s_interval_pairs) / sizeof(s_interval_pairs[0]),
        OnIntervalChanged
    );

    int cur_led_idx = 1; // 10s default
    for (int i = 0; i < (int)(sizeof(s_led_duration_pairs) / sizeof(s_led_duration_pairs[0])); i++) {
        if (s_led_duration_pairs[i].value == s_config.led_duration_sec) {
            cur_led_idx = i;
            break;
        }
    }

    WUPSConfigItemMultipleValues_AddToCategory(
        root,
        "led_duration",
        "GamePad LED Pulse Duration",
        1, // default 10s
        cur_led_idx,
        (ConfigItemMultipleValuesPair*)s_led_duration_pairs,
        sizeof(s_led_duration_pairs) / sizeof(s_led_duration_pairs[0]),
        OnLedDurationChanged
    );

    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

static void ConfigMenuClosedCallback() {
    WUPSStorageAPI_SaveStorage(false);
}

static void ClearArrivalFlags() {
    OSDynLoad_Module boss_mod;
    if (OSDynLoad_Acquire("nn_boss.rpl", &boss_mod) != OS_DYNLOAD_OK) {
        return;
    }

    typedef void (*boss_almighty_ctor_t)(void* self, uint32_t pid, const uint64_t* title_id);
    typedef void (*boss_almighty_dtor_t)(void* self);
    typedef void (*boss_almighty_set_mark_t)(void* self, bool flag);
    typedef void (*boss_almighty_set_mark_off_t)(void* self);
    typedef void (*boss_almighty_set_arrival_t)(void* self, bool flag);
    uint32_t (*boss_set_new_arrival)(const uint32_t *accountThis, uint32_t flag) = nullptr;
    uint32_t (*boss_set_new_arrival_off)(const uint32_t *accountThis) = nullptr;

    boss_almighty_ctor_t boss_almighty_ctor = nullptr;
    boss_almighty_dtor_t boss_almighty_dtor = nullptr;
    boss_almighty_set_mark_t boss_almighty_set_mark = nullptr;
    boss_almighty_set_mark_off_t boss_almighty_set_mark_off = nullptr;
    boss_almighty_set_arrival_t boss_almighty_set_arrival = nullptr;

    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss7AccountFb", (void**)&boss_set_new_arrival);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlagOff__Q3_2nn4boss7AccountFv", (void**)&boss_set_new_arrival_off);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__ct__Q3_2nn4boss13AlmightyTitleFUiQ3_2nn4boss7TitleID", (void**)&boss_almighty_ctor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__dt__Q3_2nn4boss13AlmightyTitleFv", (void**)&boss_almighty_dtor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalMarkFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_mark);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalMarkFlagOff__Q3_2nn4boss13AlmightyTitleFv", (void**)&boss_almighty_set_mark_off);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_arrival);

    nn::act::Initialize();
    const uint64_t notif_title_ids[] = {
        0x000500101004D000ULL, // JPN
        0x000500101004D100ULL, // USA
        0x000500101004D200ULL  // EUR
    };

    for (uint8_t s = 1; s <= 12; s++) {
        uint32_t pid = nn::act::GetPersistentIdEx(s);
        if (pid == 0) continue;

        if (boss_set_new_arrival_off) {
            boss_set_new_arrival_off(&pid);
        } else if (boss_set_new_arrival) {
            boss_set_new_arrival(&pid, 0);
        }

        if (boss_almighty_ctor && boss_almighty_dtor) {
            for (size_t t = 0; t < sizeof(notif_title_ids) / sizeof(notif_title_ids[0]); t++) {
                uint8_t almighty_buf[32] __attribute__((aligned(8)));
                memset(almighty_buf, 0, sizeof(almighty_buf));
                boss_almighty_ctor(almighty_buf, pid, &notif_title_ids[t]);
                if (boss_almighty_set_mark_off) {
                    boss_almighty_set_mark_off(almighty_buf);
                } else if (boss_almighty_set_mark) {
                    boss_almighty_set_mark(almighty_buf, 0);
                }
                if (boss_almighty_set_arrival) {
                    boss_almighty_set_arrival(almighty_buf, 0);
                }
                boss_almighty_dtor(almighty_buf);
            }
        }
        PluginLog("[Plugin] Cleared arrival & mark flags for PID 0x%08X", pid);
    }
    nn::act::Finalize();
}

static int WorkerThreadMain(int argc, const char **argv) {
    // Wait briefly after title boot, checking s_plugin_running every 100ms
    for (int i = 0; i < NOTIFY_BOOT_DELAY_SEC * 10 && s_plugin_running; i++) {
        OSSleepTicks(OSMillisecondsToTicks(100));
    }
    if (!s_plugin_running) return 0;

    DrcLed_Init();
    Feed_Init(NOTIFY_SD_DIR);
    NewsStore_Init(NOTIFY_SD_DIR);
    Overlay_Init();

    // Open TCP listener on port 7331 if enabled
    if (s_config.enable_tcp) {
        s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (s_server_fd >= 0) {
            int enable = 1;
            setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(PLUGIN_TCP_PORT);
            if (bind(s_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
                listen(s_server_fd, 4) < 0) {
                close(s_server_fd);
                s_server_fd = -1;
            }
        }
    }

    uint32_t ticks_since_poll = 0; // Wait full poll_interval_sec before first check

    while (s_plugin_running) {
        uint32_t target_ticks = s_config.poll_interval_sec * 4;
        if (target_ticks == 0) target_ticks = 120; // 30s safety fallback

        if (ticks_since_poll >= target_ticks) {
            ticks_since_poll = 0;

            // 1. Check SD card feed.json if present
            NotificationFeed feed;
            if (Feed_LoadFromSD(&feed)) {
                for (uint32_t i = 0; i < feed.count && s_plugin_running; i++) {
                    CommunityNotification* item = &feed.items[i];
                    if (!Feed_IsSeen(item->id)) {
                        char xml[1536];
                        FormatSpotPassXml(xml, sizeof(xml), item->title, item->message, item->app, item->feeling);
                        char status[128];
                        if (PostOfficialNotification(xml, status, sizeof(status))) {
                            if (s_config.enable_led && item->priority > 0) {
                                DrcLed_TurnOn(1);
                                s_led_active_ticks = (s_config.led_duration_sec > 0 ? s_config.led_duration_sec : 10) * 4;
                            }
                            if (s_config.enable_history_log) {
                                Feed_AppendHistory(item);
                            }
                            if (s_config.enable_screen_overlay) {
                                NotificationStyle style = NOTIFY_STYLE_SPOTPASS_BLUE;
                                if (item->type == NOTIFY_TYPE_MAINTENANCE) style = NOTIFY_STYLE_CRITICAL_RED;
                                else if (item->type == NOTIFY_TYPE_COMMUNITY) style = NOTIFY_STYLE_DARK_ELEGANT;
                                else if (item->type == NOTIFY_TYPE_FRIEND_INVITE) style = NOTIFY_STYLE_PARTY_PURPLE;
                                else if (item->type == NOTIFY_TYPE_ACHIEVEMENT) style = NOTIFY_STYLE_SUCCESS_GREEN;
                                else if (item->type == NOTIFY_TYPE_SYSTEM_UPDATE) style = NOTIFY_STYLE_WARNING_AMBER;
                                Overlay_ShowToast(item->title, item->message, item->duration_sec > 0 ? item->duration_sec : 6.0f, style);
                            }
                            Feed_MarkSeen(item->id);
                        }
                    }
                }
            }

            // 2. Autonomous on-console delivery (Zero-PC Mode)
            if (s_config.enable_autonomous && s_plugin_running) {
                CommunityNotification auto_item;
                if (Feed_GetNextAutonomous(&auto_item)) {
                    char xml[1536];
                    FormatSpotPassXml(xml, sizeof(xml), auto_item.title, auto_item.message, auto_item.app, auto_item.feeling);
                    char status[128];
                    bool ok = PostOfficialNotification(xml, status, sizeof(status));
                    PluginLog("[Plugin] Autonomous Delivery: '%s' (app=%s) -> %s", auto_item.title, auto_item.app, status);
                    if (ok) {
                        if (s_config.enable_led && auto_item.priority > 0) {
                            DrcLed_TurnOn(1);
                            s_led_active_ticks = (s_config.led_duration_sec > 0 ? s_config.led_duration_sec : 10) * 4;
                        }
                        if (s_config.enable_history_log) {
                            Feed_AppendHistory(&auto_item);
                        }
                        if (s_config.enable_screen_overlay) {
                            NotificationStyle style = NOTIFY_STYLE_SPOTPASS_BLUE;
                            if (auto_item.type == NOTIFY_TYPE_MAINTENANCE) style = NOTIFY_STYLE_CRITICAL_RED;
                            else if (auto_item.type == NOTIFY_TYPE_COMMUNITY) style = NOTIFY_STYLE_DARK_ELEGANT;
                            else if (auto_item.type == NOTIFY_TYPE_FRIEND_INVITE) style = NOTIFY_STYLE_PARTY_PURPLE;
                            else if (auto_item.type == NOTIFY_TYPE_ACHIEVEMENT) style = NOTIFY_STYLE_SUCCESS_GREEN;
                            else if (auto_item.type == NOTIFY_TYPE_SYSTEM_UPDATE) style = NOTIFY_STYLE_WARNING_AMBER;
                            Overlay_ShowToast(auto_item.title, auto_item.message, 6.0f, style);
                        }
                        Feed_MarkSeen(auto_item.id);
                    }
                }
            }
        }

        // 3. Check TCP listener with 250ms timeout if enabled
        if (s_server_fd >= 0) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(s_server_fd, &read_fds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            int sel = select(s_server_fd + 1, &read_fds, nullptr, nullptr, &tv);
            if (sel > 0 && s_plugin_running) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(s_server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd >= 0) {
                    HandleTcpClient(client_fd);
                    close(client_fd);
                }
            }
        } else {
            OSSleepTicks(OSMillisecondsToTicks(250));
        }

        // 4. GamePad input activity & LED auto-off handling
        if (s_led_active_ticks > 0) {
            VPADStatus vpad;
            VPADReadError vpad_err;
            int read_res = VPADRead(VPAD_CHAN_0, &vpad, 1, &vpad_err);
            if (read_res > 0 && vpad_err == VPAD_READ_SUCCESS) {
                // If user holds any button or touches the GamePad screen, turn off LED immediately
                if (vpad.hold != 0 || vpad.tpNormal.touched != 0) {
                    s_led_active_ticks = 0;
                    DrcLed_TurnOff();
                    PluginLog("[Plugin] LED dismissed by GamePad user interaction");
                }
            }

            if (s_led_active_ticks > 0) {
                s_led_active_ticks--;
                if (s_led_active_ticks == 0) {
                    DrcLed_TurnOff();
                    PluginLog("[Plugin] LED auto-off timer expired");
                }
            }
        }

        ticks_since_poll++;
    }

    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    Feed_Shutdown();
    DrcLed_Shutdown();
    Overlay_Shutdown();
    return 0;
}

static void StopWorkerThread() {
    if (!s_thread_active) return;
    s_plugin_running = false;
    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    OSJoinThread(&s_worker_thread, nullptr);
    s_thread_active = false;
}

INITIALIZE_PLUGIN() {
    Config_SetDefaults(&s_config);
    s_plugin_running = false;
    s_thread_active = false;

    WUPSConfigAPIOptionsV1 configOptions = { .name = "Community Notifications" };
    if (WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback) == WUPSCONFIG_API_RESULT_SUCCESS) {
        WUPSStorageAPI_GetBool(nullptr, "enable_autonomous", &s_config.enable_autonomous);
        WUPSStorageAPI_GetBool(nullptr, "enable_led", &s_config.enable_led);
        WUPSStorageAPI_GetBool(nullptr, "enable_screen_overlay", &s_config.enable_screen_overlay);
        WUPSStorageAPI_GetBool(nullptr, "enable_tcp", &s_config.enable_tcp);
        WUPSStorageAPI_GetU32(nullptr, "poll_interval_sec", &s_config.poll_interval_sec);
        WUPSStorageAPI_GetU32(nullptr, "led_duration_sec", &s_config.led_duration_sec);
        if (s_config.led_duration_sec == 0) s_config.led_duration_sec = 10;
        PluginLog("[Plugin] Storage loaded: auto=%d, led=%d, tcp=%d, interval=%u, led_dur=%u",
                  (int)s_config.enable_autonomous, (int)s_config.enable_led, (int)s_config.enable_tcp, s_config.poll_interval_sec, s_config.led_duration_sec);
    }
}

ON_APPLICATION_START() {
    StopWorkerThread();

    // Ensure GamePad LED is extinguished on any title change
    DrcLed_TurnOff();
    s_led_active_ticks = 0;

    uint64_t tid = OSGetTitleID();
    // When inside Notification Menu applet itself (00050010-1004D000/100/200):
    // the user is reading notifications -> clear arrival flags (only here, to avoid
    // needless IOSU flag writes on every title switch).
    if (tid == 0x000500101004D000ULL ||
        tid == 0x000500101004D100ULL ||
        tid == 0x000500101004D200ULL) {
        NewsStore_Init(NOTIFY_SD_DIR);
        ClearArrivalFlags();
        return;
    }

    s_plugin_running = true;
    BOOL created = OSCreateThread(
        &s_worker_thread,
        WorkerThreadMain,
        0,
        nullptr,
        s_thread_stack + sizeof(s_thread_stack),
        sizeof(s_thread_stack),
        16,
        OS_THREAD_ATTRIB_AFFINITY_CPU2
    );
    if (created) {
        s_thread_active = true;
        OSResumeThread(&s_worker_thread);
    } else {
        s_plugin_running = false;
    }
}

ON_APPLICATION_REQUESTS_EXIT() {
    StopWorkerThread();
}

ON_APPLICATION_ENDS() {
    StopWorkerThread();
}

DEINITIALIZE_PLUGIN() {
    StopWorkerThread();
}

#else
// Desktop Test Simulator
int main(int argc, char** argv) {
    printf("============================================================\n");
    printf("  Wii U Community Notification System - Desktop Simulator   \n");
    printf("  Version: %s                                               \n", WIIU_NOTIFY_VERSION_STR);
    printf("============================================================\n\n");

    Config_SetDefaults(&s_config);
    s_config.poll_interval_sec = 2; // Fast for testing

    DrcLed_Init();
    Overlay_Init();
    Feed_Init("./data");

    // Load sample feed
    NotificationFeed feed;
    const char* sample_feed_path = "server/feed_example.json";
    if (argc > 1) {
        sample_feed_path = argv[1];
    }

    printf("[Simulator] Reading feed from: %s\n", sample_feed_path);
    FILE* f = fopen(sample_feed_path, "rb");
    if (!f) {
        printf("[Simulator] Error opening %s\n", sample_feed_path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buffer = (char*)malloc(sz + 1);
    fread(buffer, 1, sz, f);
    buffer[sz] = '\0';
    fclose(f);

    if (!Feed_ParseJSON(buffer, sz, &feed)) {
        printf("[Simulator] Failed to parse feed JSON!\n");
        free(buffer);
        return 1;
    }
    free(buffer);

    printf("[Simulator] Successfully parsed %u notification(s)!\n", feed.count);

    for (uint32_t i = 0; i < feed.count; i++) {
        CommunityNotification* item = &feed.items[i];
        bool already_seen = Feed_IsSeen(item->id);
        printf("\n--- Item %u: ID '%s' [%s] (Seen: %s) ---\n", 
               i + 1, item->id, item->sender, already_seen ? "YES" : "NO");

        if (!already_seen) {
            // Pulse LED
            if (item->priority > 0) {
                DrcLed_TurnOn(1);
            }

            // Display Toast
            Overlay_ShowToast(
                item->title, 
                item->message, 
                item->duration_sec, 
                NOTIFY_STYLE_SPOTPASS_BLUE
            );

            if (item->priority > 0) {
                DrcLed_TurnOff();
            }

            Feed_AppendHistory(item);
            Feed_MarkSeen(item->id);
        }
    }

    printf("\n[Simulator] Test complete! History log & seen states written to './data/'.\n");
    DrcLed_Shutdown();
    Overlay_Shutdown();
    Feed_Shutdown();
    return 0;
}
#endif
