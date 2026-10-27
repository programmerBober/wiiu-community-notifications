#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <notifications/notifications.h>
#include <nn/acp/drcled_c.h>
#include <nn/act/client_cpp.h>
#include <stdio.h>

int main(int argc, char **argv) {
    WHBProcInit();
    WHBLogConsoleInit();

    // 1. Initialize notification system and GamePad LED
    NotificationModuleStatus notif_status = NotificationModule_InitLibrary();
    uint32_t persistentId = nn::act::GetPersistentId();
    if (persistentId == 0) persistentId = 1;

    // 2. Trigger GamePad LED breathing pulse immediately on startup!
    ACPTurnOnDrcLed(persistentId, 1);

    // 3. Trigger authentic Wii U Community Notification Toast on startup!
    NMColor cyan_bg = { 0, 162, 232, 240 };
    NMColor white_fg = { 255, 255, 255, 255 };

    if (notif_status == NOTIFICATION_MODULE_RESULT_SUCCESS) {
        NotificationModule_AddInfoNotificationEx(
            "✦ Community Notification System ✦\nWii U SpotPass Revived! GamePad LED Glowing.",
            8.0f,
            white_fg,
            cyan_bg,
            nullptr,
            nullptr
        );
    }

    uint32_t count = 0;
    VPADStatus vpad;
    VPADReadError verror;

    WHBLogPrintf("=================================================");
    WHBLogPrintf("   WII U COMMUNITY NOTIFICATIONS - TEST RUNNER   ");
    WHBLogPrintf("=================================================");
    WHBLogPrintf(" NotificationModule Status: %s", 
                 notif_status == NOTIFICATION_MODULE_RESULT_SUCCESS ? "CONNECTED (Ready)" : "Not Detected");
    WHBLogPrintf(" Persistent ID: 0x%08X", persistentId);
    WHBLogPrintf("");
    WHBLogPrintf(" CONTROLS:");
    WHBLogPrintf("  [A] Trigger SpotPass Community Announcement Toast");
    WHBLogPrintf("  [B] Trigger Warning / Error Notification");
    WHBLogPrintf("  [X] Turn ON GamePad HOME Button Breathing LED");
    WHBLogPrintf("  [Y] Turn OFF GamePad HOME Button LED");
    WHBLogPrintf("  [+] Exit back to Wii U Menu");
    WHBLogPrintf("");
    WHBLogPrintf(" Tip: Watch the GamePad HOME LED ring & TV top-left!");
    WHBLogPrintf("-------------------------------------------------");
    WHBLogConsoleDraw();

    while (WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &vpad, 1, &verror);

        if (verror == VPAD_READ_SUCCESS) {
            // [A] Send Community Announcement
            if (vpad.trigger & VPAD_BUTTON_A) {
                count++;
                char msg[256];
                snprintf(msg, sizeof(msg), "✦ Pretendo Splatfest #%u ✦\nInkopolis Plaza is active! Choose your side.", count);
                NotificationModule_AddInfoNotificationEx(
                    msg,
                    7.0f,
                    white_fg,
                    cyan_bg,
                    nullptr,
                    nullptr
                );
                ACPTurnOnDrcLed(persistentId, 1);
                WHBLogPrintf(" -> [A] Sent Community Splatfest Toast #%u & Pulsed LED!", count);
            }

            // [B] Send Error / Warning Toast
            if (vpad.trigger & VPAD_BUTTON_B) {
                NotificationModule_AddErrorNotification("✦ Alert Test ✦\nWarning: Server maintenance in 15 mins.");
                WHBLogPrintf(" -> [B] Sent Error Notification Shake Toast!");
            }

            // [X] Turn ON GamePad HOME LED
            if (vpad.trigger & VPAD_BUTTON_X) {
                ACPTurnOnDrcLed(persistentId, 1);
                WHBLogPrintf(" -> [X] GamePad HOME Button LED turned ON");
            }

            // [Y] Turn OFF GamePad HOME LED
            if (vpad.trigger & VPAD_BUTTON_Y) {
                ACPTurnOffDrcLed();
                WHBLogPrintf(" -> [Y] GamePad HOME Button LED turned OFF");
            }

            // [+] or HOME button exits
            if (vpad.trigger & (VPAD_BUTTON_PLUS | VPAD_BUTTON_HOME)) {
                WHBLogPrintf(" -> Exiting to Wii U Menu...");
                WHBLogConsoleDraw();
                break;
            }
        }

        WHBLogConsoleDraw();
        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    ACPTurnOffDrcLed();
    NotificationModule_DeInitLibrary();
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
