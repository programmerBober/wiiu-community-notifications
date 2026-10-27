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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVER_PORT 7331
#define STACK_SIZE 0x8000

static volatile bool s_server_running = false;
static uint8_t s_thread_stack[STACK_SIZE] __attribute__((aligned(32)));
static OSThread s_server_thread;
static uint32_t s_persistent_id = 1;

static NMColor s_cyan_bg = { 0, 162, 232, 240 };
static NMColor s_white_fg = { 255, 255, 255, 255 };

//-----------------------------------------------------------------------------
// Official SpotPass News Injection via nn::boss::News::PostNews & zlib125
//-----------------------------------------------------------------------------
static bool PostOfficialNotification(const char* xml_text, char* out_status, size_t out_status_sz) {
    OSDynLoad_Module zlib_mod, boss_mod;
    if (OSDynLoad_Acquire("zlib125.rpl", &zlib_mod) != OS_DYNLOAD_OK) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: zlib125.rpl missing");
        return false;
    }
    if (OSDynLoad_Acquire("nn_boss.rpl", &boss_mod) != OS_DYNLOAD_OK) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: nn_boss.rpl missing");
        return false;
    }

    int (*z_compress)(uint8_t *dest, uint32_t *destLen, const uint8_t *source, uint32_t sourceLen) = nullptr;
    uint32_t (*boss_init)() = nullptr;
    uint32_t (*boss_post_news)(const uint8_t *data, uint32_t size) = nullptr;
    uint32_t (*boss_finalize)() = nullptr;

    OSDynLoad_FindExport(zlib_mod, OS_DYNLOAD_EXPORT_FUNC, "compress", (void**)&z_compress);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Initialize__Q2_2nn4bossFv", (void**)&boss_init);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "PostNews__Q3_2nn4boss4NewsSFPUcUi", (void**)&boss_post_news);
    OSDynLoad_FindExport(boss_mod, OS_DYNLOAD_EXPORT_FUNC, "Finalize__Q2_2nn4bossFv", (void**)&boss_finalize);

    if (!z_compress || !boss_init || !boss_post_news) {
        if (out_status) snprintf(out_status, out_status_sz, "Err: symbols not found");
        return false;
    }

    uint32_t xml_len = strlen(xml_text);
    uint32_t comp_len = xml_len + 512;
    uint8_t* comp_buf = (uint8_t*)malloc(comp_len);
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

    uint32_t b_init = boss_init();
    uint32_t post_ret = boss_post_news(comp_buf, comp_len);
    if (boss_finalize) boss_finalize();
    free(comp_buf);

    if (out_status) {
        snprintf(out_status, out_status_sz, "PostNews: 0x%08X (init=0x%08X, %u B)", post_ret, b_init, comp_len);
    }
    return (post_ret == 0);
}

static void FormatSpotPassXml(char* dst, size_t dst_sz,
                             const char* title, const char* body,
                             const char* title_id, const char* feeling,
                             const char* link_type) {
    bool has_app = (title_id && strlen(title_id) >= 16 && strcmp(link_type, "APP") == 0);

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
            title, body, title_id, feeling ? feeling : "HAPPY", title_id);
    } else {
        snprintf(dst, dst_sz,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<Message>\n"
            "  <Subject>%s</Subject>\n"
            "  <Body>%s</Body>\n"
            "  <TitleId>000500101004D200</TitleId>\n"
            "  <HighPriority>true</HighPriority>\n"
            "  <ForceNotify>true</ForceNotify>\n"
            "  <Feeling>%s</Feeling>\n"
            "  <OptoutType>NEVER</OptoutType>\n"
            "  <LinkType>NONE</LinkType>\n"
            "</Message>\n",
            title, body, feeling ? feeling : "HAPPY");
    }
}

//-----------------------------------------------------------------------------
// TCP Socket Server for Desktop Notifications (Port 7331)
//-----------------------------------------------------------------------------
static int ServerThreadMain(int argc, const char** argv) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    int enable = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 4) < 0) {
        close(server_fd);
        return -1;
    }

    // Set non-blocking mode on socket
    setsockopt(server_fd, SOL_SOCKET, SO_NONBLOCK, &enable, sizeof(enable));

    char buf[2048];

    while (s_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            OSSleepTicks(OSMillisecondsToTicks(50));
            continue;
        }

        memset(buf, 0, sizeof(buf));
        int bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (bytes > 0) {
            buf[bytes] = '\0';

            // Simple parser for desktop JSON: {"title":"...","body":"...","app":"..."}
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
            snprintf(toast, sizeof(toast), "✦ Desktop Notification ✦\n%s: %s", title, body);
            NotificationModule_AddInfoNotificationEx(toast, 7.0f, s_white_fg, s_cyan_bg, nullptr, nullptr);

            send(client_fd, "OK\n", 3, 0);
        }
        close(client_fd);
    }

    close(server_fd);
    return 0;
}

//-----------------------------------------------------------------------------
// Feed Synchronizer (Imports feed.json into official SpotPass database)
//-----------------------------------------------------------------------------
static void SyncLocalFeed(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 65536) {
        fclose(f);
        return;
    }

    char* json = (char*)malloc(sz + 1);
    if (!json) {
        fclose(f);
        return;
    }

    fread(json, 1, sz, f);
    json[sz] = '\0';
    fclose(f);

    // Simple parser for feed.json notifications
    char* cur = json;
    uint32_t imported = 0;

    while ((cur = strstr(cur, "\"title\":")) != nullptr) {
        char title[128] = "Community Notice";
        char body[512] = "";

        char* q1 = strchr(cur + 8, '\"');
        if (q1) {
            char* q2 = strchr(q1 + 1, '\"');
            if (q2) {
                size_t l = q2 - (q1 + 1);
                if (l > sizeof(title) - 1) l = sizeof(title) - 1;
                strncpy(title, q1 + 1, l);
                title[l] = '\0';
            }
        }

        char* m_pos = strstr(cur, "\"message\":");
        if (m_pos) {
            q1 = strchr(m_pos + 10, '\"');
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

        if (body[0] != '\0') {
            char xml[1024];
            FormatSpotPassXml(xml, sizeof(xml), title, body, "000500101004D200", "HAPPY", "NONE");
            PostOfficialNotification(xml, nullptr, 0);
            imported++;
        }

        cur += 8;
    }

    free(json);

    if (imported > 0) {
        char toast[128];
        snprintf(toast, sizeof(toast), "✦ Feed Synced ✦\nImported %u official SpotPass notices!", imported);
        NotificationModule_AddInfoNotificationEx(toast, 6.0f, s_white_fg, s_cyan_bg, nullptr, nullptr);
        ACPTurnOnDrcLed(s_persistent_id, 1);
    }
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

    s_persistent_id = nn::act::GetPersistentId();
    if (s_persistent_id == 0) s_persistent_id = 1;
    ACPTurnOnDrcLed(s_persistent_id, 1);

    if (notif_status == NOTIFICATION_MODULE_RESULT_SUCCESS) {
        NotificationModule_AddInfoNotificationEx(
            "✦ Official Notification System Active ✦\n[A]: Splatoon | [B]: Minecraft | [X]: Pretendo\nListening on TCP :7331 for Desktop Alerts",
            10.0f,
            s_white_fg,
            s_cyan_bg,
            nullptr,
            nullptr
        );
    }

    // 1. Sync any pending community items from feed.json
    SyncLocalFeed("fs:/vol/external01/wiiu/notifications/feed.json");

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
                snprintf(toast, sizeof(toast), "✦ Splatoon Reminder Injected! ✦\n%s", status);
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
                snprintf(toast, sizeof(toast), "✦ Minecraft Reminder Injected! ✦\n%s", status);
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
                snprintf(toast, sizeof(toast), "✦ Pretendo Notice Injected! ✦\n%s", status);
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

    // Stop background socket thread
    s_server_running = false;
    OSJoinThread(&s_server_thread, nullptr);

    NotificationModule_DeInitLibrary();
    WHBGfxShutdown();
    WHBProcShutdown();
    return 0;
}
