#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#include "notification_config.h"
#include "notification_feed.h"
#include "drc_led_controller.h"
#include "notifications_overlay.h"

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

static OSThread s_worker_thread;
static uint8_t s_thread_stack[0x8000] __attribute__((aligned(0x20)));
static bool s_thread_active = false;
static int s_server_fd = -1;

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

#define BOSS_NEWS_BASE     "fs:/vol/storage_mlc01/usr/boss/00050010/1004d200/user/common"
#define BOSS_NEWS_BASE_ALT "/vol/storage_mlc01/usr/boss/00050010/1004d200/user/common"

static bool InjectNewsDirect(const uint8_t* comp_buf, uint32_t comp_len, uint32_t pid, uint32_t* out_serial) {
    char rc_path[256];
    char fad_path[256];
    char didx_path[256];
    char data_path[256];

    const char* base = BOSS_NEWS_BASE;
    snprintf(rc_path, sizeof(rc_path), "%s/data/news/ref_count.db", base);
    FILE* frc = fopen(rc_path, "r+b");
    if (!frc) {
        base = BOSS_NEWS_BASE_ALT;
        snprintf(rc_path, sizeof(rc_path), "%s/data/news/ref_count.db", base);
        frc = fopen(rc_path, "r+b");
        if (!frc) {
            PluginLog("[Plugin] DirectInject: could not open %s", rc_path);
            return false;
        }
    }

    uint8_t rc_hdr[8];
    if (fread(rc_hdr, 1, 8, frc) != 8) {
        fclose(frc);
        return false;
    }

    uint32_t next_serial = 0x000F0001;
    int rc_free_slot = -1;
    for (int i = 0; i < 512; i++) {
        uint8_t entry[0x24];
        if (fread(entry, 1, 0x24, frc) != 0x24) break;
        uint32_t s = (entry[0] << 24) | (entry[1] << 16) | (entry[2] << 8) | entry[3];
        if (s != 0) {
            if (s >= next_serial) next_serial = s + 1;
        } else if (rc_free_slot < 0) {
            rc_free_slot = i;
        }
    }
    if (rc_free_slot < 0) {
        fclose(frc);
        PluginLog("[Plugin] DirectInject: ref_count.db full");
        return false;
    }

    snprintf(fad_path, sizeof(fad_path), "%s/%08x/news/fad.db", base, pid);
    FILE* ffad = fopen(fad_path, "r+b");
    if (!ffad) {
        fclose(frc);
        PluginLog("[Plugin] DirectInject: could not open %s", fad_path);
        return false;
    }

    uint8_t fad_hdr[8];
    if (fread(fad_hdr, 1, 8, ffad) != 8) {
        fclose(frc);
        fclose(ffad);
        return false;
    }

    int fad_free_slot = -1;
    for (int i = 0; i < 512; i++) {
        uint8_t entry[0x38];
        if (fread(entry, 1, 0x38, ffad) != 0x38) break;
        bool all_zero = true;
        for (int b = 0; b < 0x38; b++) {
            if (entry[b] != 0) { all_zero = false; break; }
        }
        if (all_zero) {
            fad_free_slot = i;
            break;
        }
    }
    if (fad_free_slot < 0) {
        fclose(frc);
        fclose(ffad);
        PluginLog("[Plugin] DirectInject: fad.db full");
        return false;
    }

    snprintf(data_path, sizeof(data_path), "%s/data/news/%08x", base, next_serial);
    FILE* fdata = fopen(data_path, "wb");
    if (!fdata) {
        fclose(frc);
        fclose(ffad);
        PluginLog("[Plugin] DirectInject: could not create %s", data_path);
        return false;
    }
    fwrite(comp_buf, 1, comp_len, fdata);
    fclose(fdata);

    time_t cur_time = time(NULL);
    uint32_t secs_2000 = (cur_time > 946684800) ? (uint32_t)(cur_time - 946684800) : (uint32_t)cur_time;
    uint64_t us_2000 = (uint64_t)secs_2000 * 1000000ULL;

    uint8_t rc_entry[0x24];
    memset(rc_entry, 0, sizeof(rc_entry));
    rc_entry[0] = (next_serial >> 24) & 0xFF;
    rc_entry[1] = (next_serial >> 16) & 0xFF;
    rc_entry[2] = (next_serial >> 8) & 0xFF;
    rc_entry[3] = next_serial & 0xFF;
    rc_entry[4] = 0x01;
    rc_entry[8] = (comp_len >> 24) & 0xFF;
    rc_entry[9] = (comp_len >> 16) & 0xFF;
    rc_entry[10] = (comp_len >> 8) & 0xFF;
    rc_entry[11] = comp_len & 0xFF;
    for (int b = 0; b < 8; b++) {
        rc_entry[28 + b] = (us_2000 >> (56 - b*8)) & 0xFF;
    }
    fseek(frc, 8 + rc_free_slot * 0x24, SEEK_SET);
    fwrite(rc_entry, 1, 0x24, frc);
    fclose(frc);

    uint8_t fad_entry[0x38];
    memset(fad_entry, 0, sizeof(fad_entry));
    snprintf((char*)fad_entry, 32, "cn%x.xml", next_serial);
    fad_entry[32] = (next_serial >> 24) & 0xFF;
    fad_entry[33] = (next_serial >> 16) & 0xFF;
    fad_entry[34] = (next_serial >> 8) & 0xFF;
    fad_entry[35] = next_serial & 0xFF;
    fad_entry[52] = (secs_2000 >> 24) & 0xFF;
    fad_entry[53] = (secs_2000 >> 16) & 0xFF;
    fad_entry[54] = (secs_2000 >> 8) & 0xFF;
    fad_entry[55] = secs_2000 & 0xFF;
    fseek(ffad, 8 + fad_free_slot * 0x38, SEEK_SET);
    fwrite(fad_entry, 1, 0x38, ffad);
    fclose(ffad);

    snprintf(didx_path, sizeof(didx_path), "%s/%08x/news/didx.db", base, pid);
    FILE* fdidx = fopen(didx_path, "r+b");
    if (fdidx) {
        uint32_t bit_index = next_serial - 1;
        uint32_t byte_index = bit_index / 8;
        uint8_t bit_mask = 0x80 >> (bit_index % 8);
        fseek(fdidx, byte_index, SEEK_SET);
        uint8_t b = 0;
        if (fread(&b, 1, 1, fdidx) == 1) {
            b |= bit_mask;
            fseek(fdidx, byte_index, SEEK_SET);
            fwrite(&b, 1, 1, fdidx);
        }
        fclose(fdidx);
    }

    if (out_serial) *out_serial = next_serial;
    PluginLog("[Plugin] DirectInject SUCCESS: serial=0x%08X (len=%u) for PID=0x%08X", next_serial, comp_len, pid);
    return true;
}

static bool PostOfficialNotification(const char* xml_text, char* out_status, size_t out_status_sz) {
    OSDynLoad_Module zlib_mod, boss_mod;
    if (OSDynLoad_Acquire("zlib125.rpl", &zlib_mod) != OS_DYNLOAD_OK) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: zlib125.rpl missing");
        PluginLog("[Plugin] Err: zlib125.rpl missing");
        return false;
    }
    if (OSDynLoad_Acquire("nn_boss.rpl", &boss_mod) != OS_DYNLOAD_OK) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: nn_boss.rpl missing");
        PluginLog("[Plugin] Err: nn_boss.rpl missing");
        return false;
    }

    int (*z_compress)(uint8_t *dest, uint32_t *destLen, const uint8_t *source, uint32_t sourceLen) = nullptr;
    uint32_t (*boss_init)() = nullptr;
    uint32_t (*boss_post_news_pids)(const uint8_t *data, uint32_t size, const uint32_t *persistentIds, uint32_t numAccounts) = nullptr;
    uint32_t (*boss_post_news_plural)(const uint8_t *slotList, uint32_t slotCount, const uint8_t *data, uint32_t size) = nullptr;
    uint32_t (*boss_set_new_arrival)(const uint32_t *accountThis, uint32_t flag) = nullptr;
    uint32_t (*boss_finalize)() = nullptr;

    typedef void (*boss_almighty_ctor_t)(void* self, uint32_t pid, const uint64_t* title_id);
    typedef void (*boss_almighty_dtor_t)(void* self);
    typedef void (*boss_almighty_set_mark_t)(void* self, bool flag);
    typedef void (*boss_almighty_set_arrival_t)(void* self, bool flag);

    boss_almighty_ctor_t boss_almighty_ctor = nullptr;
    boss_almighty_dtor_t boss_almighty_dtor = nullptr;
    boss_almighty_set_mark_t boss_almighty_set_mark = nullptr;
    boss_almighty_set_arrival_t boss_almighty_set_arrival = nullptr;

    OSDynLoad_FindExport(zlib_mod, OS_DYNLOAD_EXPORT_FUNC, "compress", (void**)&z_compress);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Initialize__Q2_2nn4bossFv", (void**)&boss_init);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "PostNews__Q3_2nn4boss4NewsSFPUcUiPUiT2", (void**)&boss_post_news_pids);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "PostNewsToPluralAccount__Q3_2nn4boss4NewsSFPUcUiT1T2", (void**)&boss_post_news_plural);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss7AccountFb", (void**)&boss_set_new_arrival);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__ct__Q3_2nn4boss13AlmightyTitleFUiQ3_2nn4boss7TitleID", (void**)&boss_almighty_ctor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__dt__Q3_2nn4boss13AlmightyTitleFv", (void**)&boss_almighty_dtor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalMarkFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_mark);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_arrival);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Finalize__Q2_2nn4bossFv", (void**)&boss_finalize);

    if (!z_compress || !boss_init || !boss_post_news_pids) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: symbols not found");
        PluginLog("[Plugin] Err: symbols not found");
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

    nn::act::Initialize();
    uint32_t target_pid = nn::act::GetPersistentId();
    uint8_t target_slot = 0;
    if (target_pid != 0) {
        target_slot = nn::act::GetSlotNo();
    } else {
        for (uint8_t s = 1; s <= 12; s++) {
            uint32_t pid = nn::act::GetPersistentIdEx(s);
            if (pid != 0) {
                target_pid = pid;
                target_slot = s;
                break;
            }
        }
    }
    if (target_pid == 0) {
        target_pid = 0x80000001;
        target_slot = 1;
    }

    // Try direct MLC news storage injection first
    uint32_t direct_serial = 0;
    bool direct_ok = InjectNewsDirect(comp_buf, comp_len, target_pid, &direct_serial);

    uint32_t b_init = boss_init();
    (void)b_init;
    uint32_t post_ret = 0;
    if (!direct_ok) {
        post_ret = boss_post_news_pids(comp_buf, comp_len, &target_pid, 1);
        PluginLog("[Plugin] Fallback PostNews(pid=0x%08X, len=%u) -> 0x%08X", target_pid, comp_len, post_ret);
        if (post_ret != 0 && target_slot != 0 && boss_post_news_plural) {
            post_ret = boss_post_news_plural(&target_slot, 1, comp_buf, comp_len);
            PluginLog("[Plugin] Fallback PostNewsToPluralAccount(slot=%u) -> 0x%08X", target_slot, post_ret);
        }
    }

    if (boss_set_new_arrival) {
        uint32_t arr_ret = boss_set_new_arrival(&target_pid, 1);
        PluginLog("[Plugin] SetNewArrivalFlag(0x%08X, 1) -> 0x%08X", target_pid, arr_ret);
    }

    if (boss_almighty_ctor && boss_almighty_set_mark) {
        uint8_t almighty_buf[32] __attribute__((aligned(8)));
        memset(almighty_buf, 0, sizeof(almighty_buf));
        uint64_t notif_title_id = 0x000500101004D200ULL;
        boss_almighty_ctor(almighty_buf, target_pid, &notif_title_id);
        boss_almighty_set_mark(almighty_buf, 1);
        if (boss_almighty_set_arrival) {
            boss_almighty_set_arrival(almighty_buf, 1);
        }
        if (boss_almighty_dtor) {
            boss_almighty_dtor(almighty_buf);
        }
        PluginLog("[Plugin] AlmightyTitle mark & arrival set for PID 0x%08X", target_pid);
    }

    if (boss_finalize) {
        boss_finalize();
    }
    nn::act::Finalize();
    free(comp_buf);

    if (direct_ok) {
        if (out_status) snprintf(out_status, out_status_sz, "OK: direct=0x%08X (PID=0x%08X)", direct_serial, target_pid);
        return true;
    } else {
        if (out_status) snprintf(out_status, out_status_sz, "post=0x%08X (PID=0x%08X)", post_ret, target_pid);
        return (post_ret == 0);
    }
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
    WUPSStorageAPI_StoreBool(nullptr, "enable_led", newValue);
    PluginLog("[Plugin] Config: enable_led = %d", (int)newValue);
}

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
    typedef void (*boss_almighty_set_arrival_t)(void* self, bool flag);
    uint32_t (*boss_set_new_arrival)(const uint32_t *accountThis, uint32_t flag) = nullptr;

    boss_almighty_ctor_t boss_almighty_ctor = nullptr;
    boss_almighty_dtor_t boss_almighty_dtor = nullptr;
    boss_almighty_set_mark_t boss_almighty_set_mark = nullptr;
    boss_almighty_set_arrival_t boss_almighty_set_arrival = nullptr;

    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss7AccountFb", (void**)&boss_set_new_arrival);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__ct__Q3_2nn4boss13AlmightyTitleFUiQ3_2nn4boss7TitleID", (void**)&boss_almighty_ctor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "__dt__Q3_2nn4boss13AlmightyTitleFv", (void**)&boss_almighty_dtor);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalMarkFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_mark);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss13AlmightyTitleFb", (void**)&boss_almighty_set_arrival);

    nn::act::Initialize();
    uint32_t target_pid = nn::act::GetPersistentId();
    if (target_pid == 0) {
        for (uint8_t s = 1; s <= 12; s++) {
            uint32_t pid = nn::act::GetPersistentIdEx(s);
            if (pid != 0) {
                target_pid = pid;
                break;
            }
        }
    }

    if (target_pid != 0) {
        if (boss_set_new_arrival) {
            boss_set_new_arrival(&target_pid, 0);
        }
        if (boss_almighty_ctor && boss_almighty_set_mark) {
            uint8_t almighty_buf[32] __attribute__((aligned(8)));
            memset(almighty_buf, 0, sizeof(almighty_buf));
            uint64_t notif_title_id = 0x000500101004D200ULL;
            boss_almighty_ctor(almighty_buf, target_pid, &notif_title_id);
            boss_almighty_set_mark(almighty_buf, 0);
            if (boss_almighty_set_arrival) {
                boss_almighty_set_arrival(almighty_buf, 0);
            }
            if (boss_almighty_dtor) {
                boss_almighty_dtor(almighty_buf);
            }
        }
        PluginLog("[Plugin] Cleared arrival & mark flags for PID 0x%08X", target_pid);
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
                            }
                            if (s_config.enable_history_log) {
                                Feed_AppendHistory(item);
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
                        }
                        if (s_config.enable_history_log) {
                            Feed_AppendHistory(&auto_item);
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

        ticks_since_poll++;
    }

    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    Feed_Shutdown();
    DrcLed_Shutdown();
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
        PluginLog("[Plugin] Storage loaded: auto=%d, led=%d, tcp=%d, interval=%u",
                  (int)s_config.enable_autonomous, (int)s_config.enable_led, (int)s_config.enable_tcp, s_config.poll_interval_sec);
    }
}

ON_APPLICATION_START() {
    StopWorkerThread();

    uint64_t tid = OSGetTitleID();
    // When inside Notification Menu applet itself (00050010-1004D000/100/200)
    if (tid == 0x000500101004D000ULL ||
        tid == 0x000500101004D100ULL ||
        tid == 0x000500101004D200ULL) {
        // User opened Notification Menu: turn off LED & clear unread arrival flags
        DrcLed_TurnOff();
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
