#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "notification_config.h"
#include "notification_feed.h"
#include "drc_led_controller.h"
#include "notifications_overlay.h"

static CommunityNotifyConfig s_config;
static volatile bool s_plugin_running = false;

#if defined(__WIIU__) || defined(__WUT__) || defined(__WUPS__)
#include <wups.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/debug.h>

WUPS_PLUGIN_NAME("CommunityNotifications");
WUPS_PLUGIN_DESCRIPTION("Revives Wii U SpotPass & Community Alerts");
WUPS_PLUGIN_VERSION(WIIU_NOTIFY_VERSION_STR);
WUPS_PLUGIN_AUTHOR("programmerBober");
WUPS_PLUGIN_LICENSE("GPLv3");

WUPS_USE_WUT_DEVOPTAB();

static OSThread s_worker_thread;
static uint8_t s_thread_stack[0x4000] __attribute__((aligned(0x20)));

static int WorkerThreadMain(int argc, const char **argv) {
    OSReport("[CommunityNotifications] Background notification worker starting up...\n");

    // Give system services time to spin up after cold boot
    OSSleepTicks(OSSecondsToTicks(NOTIFY_BOOT_DELAY_SEC));

    DrcLed_Init();
    Overlay_Init();
    Feed_Init(NOTIFY_SD_DIR);

    while (s_plugin_running) {
        NotificationFeed feed;
        if (Feed_LoadFromSD(&feed)) {
            OSReport("[CommunityNotifications] Loaded feed with %u notification(s)\n", feed.count);

            for (uint32_t i = 0; i < feed.count && s_plugin_running; i++) {
                CommunityNotification* item = &feed.items[i];
                if (!Feed_IsSeen(item->id)) {
                    OSReport("[CommunityNotifications] New notification: [%s] %s\n", item->id, item->title);

                    // 1. Trigger the iconic breathing blue GamePad HOME LED
                    if (s_config.enable_led && item->priority > 0) {
                        DrcLed_TurnOn(1);
                    }

                    // 2. Display the signature on-screen toast overlay
                    if (s_config.enable_screen_overlay) {
                        Overlay_ShowToast(
                            item->title, 
                            item->message, 
                            item->duration_sec > 0 ? item->duration_sec : 7.0f,
                            NOTIFY_STYLE_SPOTPASS_BLUE
                        );
                    }

                    // Keep LED alive during toast presentation
                    if (s_config.enable_led && item->priority > 0) {
                        uint32_t led_time = (uint32_t)((item->duration_sec > 0 ? item->duration_sec : 7.0f) * 1000.0f);
                        OSSleepTicks(OSMillisecondsToTicks(led_time));
                        DrcLed_TurnOff();
                    }

                    // 3. Save to local log and mark seen
                    if (s_config.enable_history_log) {
                        Feed_AppendHistory(item);
                    }
                    Feed_MarkSeen(item->id);

                    // Small breather between consecutive notifications
                    OSSleepTicks(OSSecondsToTicks(2));
                }
            }
        }

        // Sleep poll interval or until signaled
        for (uint32_t sec = 0; sec < s_config.poll_interval_sec && s_plugin_running; sec++) {
            OSSleepTicks(OSSecondsToTicks(1));
        }
    }

    OSReport("[CommunityNotifications] Background notification worker exiting.\n");
    return 0;
}

INITIALIZE_PLUGIN() {
    OSReport("[CommunityNotifications] Initializing plugin %s...\n", WIIU_NOTIFY_VERSION_STR);
    Config_SetDefaults(&s_config);
    s_plugin_running = true;

    BOOL created = OSCreateThread(
        &s_worker_thread,
        WorkerThreadMain,
        0,
        nullptr,
        s_thread_stack + sizeof(s_thread_stack),
        sizeof(s_thread_stack),
        16, // Priority (cooperative background)
        OS_THREAD_ATTRIB_AFFINITY_CPU2
    );

    if (created) {
        OSResumeThread(&s_worker_thread);
    }
}

DEINITIALIZE_PLUGIN() {
    OSReport("[CommunityNotifications] Deinitializing plugin...\n");
    s_plugin_running = false;
    OSJoinThread(&s_worker_thread, nullptr);

    DrcLed_Shutdown();
    Overlay_Shutdown();
    Feed_Shutdown();
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
