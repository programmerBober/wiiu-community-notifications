#include <whb/proc.h>
#include <whb/gfx.h>
#include <sysapp/launch.h>
#include <coreinit/dynload.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <notifications/notifications.h>
#include <nn/acp/drcled_c.h>
#include <nn/act/client_cpp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define SERVER_PORT 7331
#define STACK_SIZE 0x8000
#define LOG_FILE_MLC "fs:/vol/storage_mlc01/usr/notifications/post_log.txt"
#define LOG_FILE_SD  "fs:/vol/external01/wiiu/notifications/post_log.txt"

static volatile bool s_server_running = false;
static int s_server_fd = -1;
static uint8_t s_thread_stack[STACK_SIZE] __attribute__((aligned(32)));
static OSThread s_server_thread;
static uint32_t s_persistent_id = 0;

static NMColor s_cyan_bg = { 0, 162, 232, 240 };
static NMColor s_white_fg = { 255, 255, 255, 255 };

static void LogMessage(const char* fmt, ...) {
    FILE* f = fopen(LOG_FILE_SD, "a");
    if (!f) {
        f = fopen(LOG_FILE_MLC, "a");
    }
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

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

//-----------------------------------------------------------------------------
// Official SpotPass News Injection via nn::boss::News::PostNews & zlib125
//-----------------------------------------------------------------------------
static bool PostOfficialNotification(const char* xml_text, char* out_status, size_t out_status_sz) {
    LogMessage("PostOfficialNotification: starting...");
    OSDynLoad_Module zlib_mod, boss_mod;
    if (OSDynLoad_Acquire("zlib125.rpl", &zlib_mod) != OS_DYNLOAD_OK) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: zlib125.rpl missing");
        LogMessage("Err: zlib125.rpl missing");
        return false;
    }
    if (OSDynLoad_Acquire("nn_boss.rpl", &boss_mod) != OS_DYNLOAD_OK) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: nn_boss.rpl missing");
        LogMessage("Err: nn_boss.rpl missing");
        return false;
    }

    int (*z_compress)(uint8_t *dest, uint32_t *destLen, const uint8_t *source, uint32_t sourceLen) = nullptr;
    uint32_t (*boss_init)() = nullptr;
    uint32_t (*boss_post_news_pids)(const uint8_t *data, uint32_t size, const uint32_t *persistentIds, uint32_t numAccounts) = nullptr;
    uint32_t (*boss_post_news_plural)(const uint8_t *slotList, uint32_t slotCount, const uint8_t *data, uint32_t size) = nullptr;
    uint32_t (*boss_set_new_arrival)(const uint32_t *accountThis, uint32_t flag) = nullptr;
    uint32_t (*boss_finalize)() = nullptr;

    OSDynLoad_FindExport(zlib_mod, OS_DYNLOAD_EXPORT_FUNC, "compress", (void**)&z_compress);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Initialize__Q2_2nn4bossFv", (void**)&boss_init);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "PostNews__Q3_2nn4boss4NewsSFPUcUiPUiT2", (void**)&boss_post_news_pids);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "PostNewsToPluralAccount__Q3_2nn4boss4NewsSFPUcUiT1T2", (void**)&boss_post_news_plural);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "SetNewArrivalFlag__Q3_2nn4boss7AccountFb", (void**)&boss_set_new_arrival);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Finalize__Q2_2nn4bossFv", (void**)&boss_finalize);

    if (!z_compress || !boss_init || !boss_post_news_pids) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: symbols not found");
        LogMessage("Err: symbols not found");
        return false;
    }

    uint32_t xml_len = strlen(xml_text);
    uint32_t comp_len = xml_len + 512;
    uint8_t* comp_buf = (uint8_t*)malloc(comp_len);
    if (!comp_buf) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: OOM");
        LogMessage("Err: OOM");
        return false;
    }

    int z_ret = z_compress(comp_buf, &comp_len, (const uint8_t*)xml_text, xml_len);
    if (z_ret != 0) {
        free(comp_buf);
        if (out_status) snprintf(out_status, out_status_sz, "Err: zlib %d", z_ret);
        LogMessage("Err: zlib %d", z_ret);
        return false;
    }
    LogMessage("Compressed XML %u -> %u bytes", xml_len, comp_len);

    // 1. Initialize nn::act & nn::boss
    nn::act::Initialize();
    uint32_t b_init = boss_init();
    LogMessage("boss_init returned 0x%08X", b_init);

    // 2. Target active profile
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

    // 3. Post news into official Nintendo notification menu for Pretendo profile
    LogMessage("Calling boss_post_news_pids(pid=0x%08X, count=1, len=%u)...", target_pid, comp_len);
    uint32_t post_ret = boss_post_news_pids(comp_buf, comp_len, &target_pid, 1);
    LogMessage("boss_post_news_pids returned 0x%08X", post_ret);

    if (post_ret != 0 && target_slot != 0 && boss_post_news_plural) {
        LogMessage("Fallback boss_post_news_plural(slot=%u, len=%u)...", target_slot, comp_len);
        post_ret = boss_post_news_plural(&target_slot, 1, comp_buf, comp_len);
        LogMessage("boss_post_news_plural returned 0x%08X", post_ret);
    }

    if (post_ret == 0 && boss_set_new_arrival) {
        uint32_t arr_ret = boss_set_new_arrival(&target_pid, 1);
        LogMessage("SetNewArrivalFlag(0x%08X, 1) returned 0x%08X", target_pid, arr_ret);
    }

    if (boss_finalize) {
        boss_finalize();
        LogMessage("boss_finalize finished");
    }
    free(comp_buf);

    if (out_status) {
        snprintf(out_status, out_status_sz, "post=0x%08X (PID=0x%08X)", post_ret, target_pid);
    }
    return (post_ret == 0);
}

static void FormatSpotPassXml(char* dst, size_t dst_sz,
                             const char* title, const char* body,
                             const char* title_id, const char* feeling,
                             const char* link_type) {
    char safe_title[256];
    char safe_body[768];
    EscapeXmlString(title ? title : "Notice", safe_title, sizeof(safe_title));
    EscapeXmlString(body ? body : "", safe_body, sizeof(safe_body));

    const char* safe_feeling = (feeling && feeling[0]) ? feeling : "HAPPY";
    bool has_app = (title_id && strlen(title_id) == 16 &&
                    strcmp(link_type, "APP") == 0 &&
                    strcmp(title_id, "000500101004D200") != 0 &&
                    strcmp(title_id, "000500101004d200") != 0);

    if (has_app) {
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

//-----------------------------------------------------------------------------
// TCP Socket Server for Desktop Notifications (Port 7331)
//-----------------------------------------------------------------------------
static int ServerThreadMain(int argc, const char** argv) {
    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) return -1;

    int enable = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(s_server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(s_server_fd);
        s_server_fd = -1;
        return -1;
    }

    if (listen(s_server_fd, 4) < 0) {
        close(s_server_fd);
        s_server_fd = -1;
        return -1;
    }

    char buf[2048];

    while (s_server_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s_server_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000; // 250ms timeout

        int sel = select(s_server_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (sel <= 0) {
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(s_server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            continue;
        }

        memset(buf, 0, sizeof(buf));
        int bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (bytes > 0) {
            buf[bytes] = '\0';

            char title[128] = "Desktop Notification";
            char body[512] = "New notification received from desktop.";
            char app[32] = "";
            char feeling[32] = "HAPPY";

            char* t_pos = strstr(buf, "\"title\":");
            if (t_pos) {
                char* q1 = strchr(t_pos + 8, '\"');
                if (q1) {
                    char* q2 = strchr(q1 + 1, '\"');
                    if (q2) {
                        size_t l = q2 - (q1 + 1);
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
                        size_t l = q2 - (q1 + 1);
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
                        size_t l = q2 - (q1 + 1);
                        if (l > sizeof(app) - 1) l = sizeof(app) - 1;
                        strncpy(app, q1 + 1, l);
                        app[l] = '\0';
                    }
                }
            }

            char xml[1024];
            FormatSpotPassXml(xml, sizeof(xml), title, body, app, feeling, (strlen(app) >= 16) ? "APP" : "NONE");

            char status[128];
            PostOfficialNotification(xml, status, sizeof(status));
            ACPTurnOnDrcLed(s_persistent_id, 1);

            char toast[256];
            snprintf(toast, sizeof(toast), "✦ Desktop Alert Injected! ✦\n%s", status);
            NotificationModule_AddInfoNotificationEx(toast, 7.0f, s_white_fg, s_cyan_bg, nullptr, nullptr);

            send(client_fd, "OK\n", 3, 0);
        }
        close(client_fd);
    }

    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    return 0;
}

//-----------------------------------------------------------------------------
// Application Entry Point
//-----------------------------------------------------------------------------
int main(int argc, char **argv) {
    WHBProcInit();

    if (!WHBGfxInit()) {
        SYSLaunchMenu();
        WHBProcShutdown();
        return 0;
    }

    NotificationModuleStatus notif_status = NotificationModule_InitLibrary();

    nn::act::Initialize();
    s_persistent_id = nn::act::GetPersistentId();
    if (s_persistent_id == 0) {
        for (uint8_t s = 1; s <= 12; s++) {
            uint32_t pid = nn::act::GetPersistentIdEx(s);
            if (pid != 0) {
                s_persistent_id = pid;
                break;
            }
        }
    }
    if (s_persistent_id == 0) s_persistent_id = 0x80000001;

    LogMessage("=== NotifyTest Started ===");

    // 1. AUTOMATICALLY INJECT SPLATOON NOTIFICATION ON STARTUP!
    char startup_xml[1024];
    FormatSpotPassXml(startup_xml, sizeof(startup_xml),
        "Splatoon",
        "Inkopolis is calling! It's been a while since your last Turf War. Jump back in and claim your turf!",
        "0005000010176A00", "HAPPY", "APP");

    char startup_status[128];
    bool injected_ok = PostOfficialNotification(startup_xml, startup_status, sizeof(startup_status));
    ACPTurnOnDrcLed(s_persistent_id, 1);

    if (notif_status == NOTIFICATION_MODULE_RESULT_SUCCESS) {
        char toast[256];
        snprintf(toast, sizeof(toast),
            "✦ Splatoon Notification Injected! ✦\n%s\nPress [+] or HOME to exit!",
            startup_status);
        NotificationModule_AddInfoNotificationEx(
            toast,
            12.0f,
            s_white_fg,
            s_cyan_bg,
            nullptr,
            nullptr
        );
    }

    // 2. Launch background TCP listener for Desktop Bridge on port 7331
    s_server_running = true;
    OSCreateThread(&s_server_thread, (OSThreadEntryPointFn)ServerThreadMain, 0, nullptr,
                   s_thread_stack + sizeof(s_thread_stack), sizeof(s_thread_stack),
                   16, OS_THREAD_ATTRIB_AFFINITY_ANY);
    OSResumeThread(&s_server_thread);

    VPADStatus vpad;
    VPADReadError verror;

    while (WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &vpad, 1, &verror);

        if (verror == VPAD_READ_SUCCESS) {
            // [A] Splatoon Inactivity Reminder into Official Nintendo Menu
            if (vpad.trigger & VPAD_BUTTON_A) {
                char xml[1024];
                FormatSpotPassXml(xml, sizeof(xml),
                    "Splatoon",
                    "Inkopolis is calling! It's been a while since your last Turf War. Jump back in and claim your turf!",
                    "0005000010176A00", "HAPPY", "APP");

                char status[128];
                PostOfficialNotification(xml, status, sizeof(status));
                ACPTurnOnDrcLed(s_persistent_id, 1);

                char toast[256];
                snprintf(toast, sizeof(toast), "✦ Splatoon Reminder ✦\n%s", status);
                NotificationModule_AddInfoNotificationEx(toast, 6.0f, s_white_fg, s_cyan_bg, nullptr, nullptr);
            }

            // [B] Minecraft Inactivity Reminder into Official Nintendo Menu
            if (vpad.trigger & VPAD_BUTTON_B) {
                char xml[1024];
                FormatSpotPassXml(xml, sizeof(xml),
                    "Minecraft: Wii U Edition",
                    "Your worlds miss you! Build, craft, and explore new adventures with your friends.",
                    "00050000101D7500", "HAPPY", "APP");

                char status[128];
                PostOfficialNotification(xml, status, sizeof(status));
                ACPTurnOnDrcLed(s_persistent_id, 1);

                char toast[256];
                snprintf(toast, sizeof(toast), "✦ Minecraft Reminder ✦\n%s", status);
                NotificationModule_AddInfoNotificationEx(toast, 6.0f, s_white_fg, s_cyan_bg, nullptr, nullptr);
            }

            // [X] Pretendo Network Official Announcement
            if (vpad.trigger & VPAD_BUTTON_X) {
                char xml[1024];
                FormatSpotPassXml(xml, sizeof(xml),
                    "Pretendo Network",
                    "SpotPass notifications revived by Community Notifications! Welcome to the open network.",
                    "000500101004D200", "HAPPY", "NONE");

                char status[128];
                PostOfficialNotification(xml, status, sizeof(status));
                ACPTurnOnDrcLed(s_persistent_id, 1);

                char toast[256];
                snprintf(toast, sizeof(toast), "✦ Pretendo Notice ✦\n%s", status);
                NotificationModule_AddInfoNotificationEx(toast, 6.0f, s_white_fg, s_cyan_bg, nullptr, nullptr);
            }

            // [Y] Toggle GamePad Breathing LED
            if (vpad.trigger & VPAD_BUTTON_Y) {
                static bool led_on = true;
                led_on = !led_on;
                if (led_on) ACPTurnOnDrcLed(s_persistent_id, 1);
                else ACPTurnOffDrcLed();
            }

            // [+] or [-] or HOME exits smoothly back to Wii U Menu
            if (vpad.trigger & (VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS | VPAD_BUTTON_HOME)) {
                SYSLaunchMenu();
            }
        }

        WHBGfxBeginRender();
        WHBGfxBeginRenderTV();
        WHBGfxClearColor(0.06f, 0.10f, 0.16f, 1.0f);
        WHBGfxFinishRenderTV();

        WHBGfxBeginRenderDRC();
        WHBGfxClearColor(0.06f, 0.10f, 0.16f, 1.0f);
        WHBGfxFinishRenderDRC();

        WHBGfxFinishRender();
    }

    // Stop background socket thread cleanly and safely!
    s_server_running = false;
    OSJoinThread(&s_server_thread, nullptr);

    NotificationModule_DeInitLibrary();
    nn::act::Finalize();
    WHBGfxShutdown();
    WHBProcShutdown();
    return 0;
}
